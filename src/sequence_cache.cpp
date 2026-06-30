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

// Pick the Empty slot nearest the focus that is worth decoding. "Worth it" means
// either we are under budget, or the candidate is closer to the focus than the
// farthest resident frame (so loading it would evict something we want less).
int SequenceCache::PickNextLocked() const
{
    const int n = static_cast<int>(m_slots.size());
    if (n == 0)
        return -1;

    // Nearest Empty slot to focus (search outward).
    int candidate = -1;
    for (int d = 0; d < n; ++d)
    {
        int lo = m_focus - d;
        int hi = m_focus + d;
        if (lo >= 0 && m_slots[lo].state == SlotState::Empty)
        {
            candidate = lo;
            break;
        }
        if (hi != lo && hi < n && m_slots[hi].state == SlotState::Empty)
        {
            candidate = hi;
            break;
        }
    }
    if (candidate < 0)
        return -1; // nothing Empty

    if (m_bytes < m_budget)
        return candidate; // room to spare

    // At/over budget: only load the candidate if it is closer than the farthest
    // resident frame (otherwise it would just evict itself).
    int farthest = -1, farDist = -1;
    for (int i = 0; i < n; ++i)
    {
        if (m_slots[i].state == SlotState::Ready)
        {
            int dist = std::abs(i - m_focus);
            if (dist > farDist)
            {
                farDist = dist;
                farthest = i;
            }
        }
    }
    (void)farthest;
    if (std::abs(candidate - m_focus) < farDist)
        return candidate;
    return -1;
}

void SequenceCache::EvictLocked(int keepIndex)
{
    const int n = static_cast<int>(m_slots.size());
    while (m_bytes > m_budget)
    {
        // Drop the resident frame farthest from focus (never the just-loaded one).
        int victim = -1, victimDist = -1;
        for (int i = 0; i < n; ++i)
        {
            if (i == keepIndex)
                continue;
            if (m_slots[i].state == SlotState::Ready && m_slots[i].image.IsLoaded())
            {
                int dist = std::abs(i - m_focus);
                if (dist > victimDist)
                {
                    victimDist = dist;
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
