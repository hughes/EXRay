// SPDX-License-Identifier: GPL-3.0-or-later

#include "sequence_cache.h"

#include <cstdlib>

SequenceCache::~SequenceCache() { StopWorker(); }

size_t SequenceCache::FrameBytes(const ImageData& img)
{
    return img.pixels.size() * sizeof(half) + sizeof(ImageData);
}

void SequenceCache::StartWorker()
{
    m_stop = false;
    m_worker = std::thread([this] { WorkerLoop(); });
}

void SequenceCache::StopWorker()
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_worker.joinable())
        m_worker.join();
}

void SequenceCache::Configure(const std::wstring& id, std::vector<std::wstring> paths, size_t memoryBudgetBytes)
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (id == m_id && paths.size() == m_paths.size())
            return; // already configured for this sequence — keep the warm cache
    }

    // Tear down any existing worker, then re-arm for the new sequence.
    StopWorker();

    std::lock_guard<std::mutex> lk(m_mutex);
    m_generation++;
    m_id = id;
    m_paths = std::move(paths);
    m_slots.assign(m_paths.size(), Slot{});
    m_focus = 0;
    m_budget = memoryBudgetBytes;
    m_bytes = 0;

    if (!m_paths.empty())
        StartWorker();
}

void SequenceCache::Clear()
{
    StopWorker();

    std::lock_guard<std::mutex> lk(m_mutex);
    m_generation++;
    m_id.clear();
    m_paths.clear();
    m_slots.clear();
    m_focus = 0;
    m_bytes = 0;
}

void SequenceCache::SetFocus(int index)
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (index < 0 || index >= static_cast<int>(m_slots.size()))
            return;
        m_focus = index;
    }
    m_cv.notify_all();
}

void SequenceCache::SetReverseLookback(int frames)
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (frames < 0)
            frames = 0;
        if (frames == m_reverseLookback)
            return;
        m_reverseLookback = frames;
    }
    m_cv.notify_all();
}

bool SequenceCache::TryGet(int index, ImageData& out)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (index < 0 || index >= static_cast<int>(m_slots.size()))
        return false;
    const Slot& s = m_slots[index];
    if (s.state != SlotState::Ready || !s.image.IsLoaded())
        return false;
    out = s.image; // copy out; the cache retains its own resident copy
    return true;
}

void SequenceCache::Put(int index, const ImageData& img)
{
    if (!img.IsLoaded())
        return;
    bool inserted = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (index < 0 || index >= static_cast<int>(m_slots.size()))
            return;
        Slot& s = m_slots[index];
        if (s.state != SlotState::Empty) // already Ready, or the worker is on it
            return;
        s.image = img;
        s.state = SlotState::Ready;
        m_bytes += FrameBytes(s.image);
        EvictLocked(index);
        inserted = true;
    }
    if (inserted)
        m_cv.notify_all();
}

int SequenceCache::ReadyCount() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    int n = 0;
    for (const Slot& s : m_slots)
        if (s.state == SlotState::Ready && s.image.IsLoaded())
            ++n;
    return n;
}

void SequenceCache::SnapshotStates(std::vector<uint8_t>& out) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    out.resize(m_slots.size());
    for (size_t i = 0; i < m_slots.size(); ++i)
    {
        const Slot& s = m_slots[i];
        if (s.state == SlotState::Ready && s.image.IsLoaded())
            out[i] = 2;
        else if (s.state == SlotState::Loading)
            out[i] = 1;
        else
            out[i] = 0;
    }
}

// Pick the Empty slot nearest the focus that is worth decoding. "Worth it" means
// either we are under budget, or the candidate is closer to the focus than the
// farthest resident frame (so loading it would evict something we want less).
int SequenceCache::PickNextLocked() const
{
    const int n = static_cast<int>(m_slots.size());
    if (n == 0)
        return -1;

    // Highest-priority (lowest value) Empty slot.
    int candidate = -1;
    long candPri = 0;
    for (int i = 0; i < n; ++i)
    {
        if (m_slots[i].state != SlotState::Empty)
            continue;
        long p = FramePriorityLocked(i);
        if (candidate < 0 || p < candPri)
        {
            candPri = p;
            candidate = i;
        }
    }
    if (candidate < 0)
        return -1; // nothing Empty

    if (m_bytes < m_budget)
        return candidate; // room to spare

    // At/over budget: only load the candidate if it outranks the worst (lowest
    // priority) resident frame — otherwise it would just evict itself.
    long worstPri = -1;
    for (int i = 0; i < n; ++i)
    {
        if (m_slots[i].state == SlotState::Ready)
            worstPri = (std::max)(worstPri, FramePriorityLocked(i));
    }
    if (candPri < worstPri)
        return candidate;
    return -1;
}

long SequenceCache::FramePriorityLocked(int i) const
{
    const int n = static_cast<int>(m_slots.size());
    if (n == 0 || i == m_focus)
        return 0;
    int fwd = (i - m_focus + n) % n;  // steps forward (play direction), wrapping
    int back = (m_focus - i + n) % n; // steps backward
    if (m_reverseLookback > 0 && back <= m_reverseLookback)
        return back; // small reverse buffer for back-scrubbing (ping-pong)
    return static_cast<long>(m_reverseLookback) + fwd;
}

void SequenceCache::EvictLocked(int keepIndex)
{
    const int n = static_cast<int>(m_slots.size());
    while (m_bytes > m_budget)
    {
        // Drop the lowest-priority resident frame (never the just-loaded one).
        int victim = -1;
        long victimPri = -1;
        for (int i = 0; i < n; ++i)
        {
            if (i == keepIndex)
                continue;
            if (m_slots[i].state == SlotState::Ready && m_slots[i].image.IsLoaded())
            {
                long p = FramePriorityLocked(i);
                if (p > victimPri)
                {
                    victimPri = p;
                    victim = i;
                }
            }
        }
        if (victim < 0)
            break; // nothing left to evict

        m_bytes -= FrameBytes(m_slots[victim].image);
        m_slots[victim].image = ImageData{};
        m_slots[victim].state = SlotState::Empty;
    }
}

void SequenceCache::WorkerLoop()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    while (!m_stop)
    {
        int idx = PickNextLocked();
        if (idx < 0)
        {
            m_cv.wait(lk);
            continue;
        }

        m_slots[idx].state = SlotState::Loading;
        std::wstring path = m_paths[idx];
        uint64_t gen = m_generation;

        lk.unlock();
        ImageData img;
        std::string err;
        bool ok = ImageLoader::LoadEXR(path, img, err);
        lk.lock();

        // If the sequence was reconfigured while we were decoding, discard.
        if (m_stop || gen != m_generation)
            continue;
        if (idx >= static_cast<int>(m_slots.size()) || m_slots[idx].state != SlotState::Loading)
            continue;

        if (ok)
        {
            m_slots[idx].image = std::move(img);
            m_slots[idx].state = SlotState::Ready;
            m_bytes += FrameBytes(m_slots[idx].image);
            EvictLocked(idx);
        }
        else
        {
            // Mark Ready-but-empty so we don't spin re-decoding a bad frame;
            // TryGet treats it as a miss and the caller surfaces the error.
            m_slots[idx].state = SlotState::Ready;
        }
        m_cv.notify_all();
    }
}
