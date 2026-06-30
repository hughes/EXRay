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
    if (m_workerStarted)
        return;
    m_stop = false;
    m_workerStarted = true;
    m_worker = std::thread([this] { WorkerLoop(); });
}

void SequenceCache::StopWorker()
{
    if (!m_workerStarted)
        return;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_worker.joinable())
        m_worker.join();
    m_workerStarted = false;
}

SequenceCache::Seq* SequenceCache::ActiveSeqLocked()
{
    if (m_activeId.empty())
        return nullptr;
    auto it = m_seqs.find(m_activeId);
    return (it == m_seqs.end()) ? nullptr : &it->second;
}

const SequenceCache::Seq* SequenceCache::ActiveSeqLocked() const
{
    if (m_activeId.empty())
        return nullptr;
    auto it = m_seqs.find(m_activeId);
    return (it == m_seqs.end()) ? nullptr : &it->second;
}

void SequenceCache::SetBudget(size_t bytes)
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_budget = bytes;
        EvictLocked(L"", -1);
    }
    m_cv.notify_all(); // a raised budget may let the worker resume prefetching
}

void SequenceCache::Activate(const std::wstring& id, std::vector<std::wstring> paths)
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_activeId = id;
        m_focus = 0;
        auto it = m_seqs.find(id);
        if (it == m_seqs.end())
        {
            Seq seq;
            seq.paths = std::move(paths);
            seq.slots.assign(seq.paths.size(), Slot{});
            m_seqs.emplace(id, std::move(seq));
        }
        else if (it->second.paths.size() != paths.size())
        {
            // Frame count changed (e.g. a rescan) — rebuild this sequence's slots.
            for (const Slot& s : it->second.slots)
                if (s.state == SlotState::Ready && s.image.IsLoaded())
                    m_bytes -= FrameBytes(s.image);
            it->second.paths = std::move(paths);
            it->second.slots.assign(it->second.paths.size(), Slot{});
        }
        StartWorker();
    }
    m_cv.notify_all();
}

void SequenceCache::Deactivate()
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_activeId.clear();
    }
    m_cv.notify_all();
}

void SequenceCache::Remove(const std::wstring& id)
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_seqs.find(id);
        if (it == m_seqs.end())
            return;
        for (const Slot& s : it->second.slots)
            if (s.state == SlotState::Ready && s.image.IsLoaded())
                m_bytes -= FrameBytes(s.image);
        m_seqs.erase(it);
        if (m_activeId == id)
            m_activeId.clear();
    }
    m_cv.notify_all();
}

void SequenceCache::Clear()
{
    StopWorker();

    std::lock_guard<std::mutex> lk(m_mutex);
    m_seqs.clear();
    m_activeId.clear();
    m_focus = 0;
    m_bytes = 0;
}

void SequenceCache::SetFocus(int index)
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        const Seq* seq = ActiveSeqLocked();
        if (!seq || index < 0 || index >= static_cast<int>(seq->slots.size()))
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
    Seq* seq = ActiveSeqLocked();
    if (!seq || index < 0 || index >= static_cast<int>(seq->slots.size()))
        return false;
    Slot& s = seq->slots[index];
    if (s.state != SlotState::Ready || !s.image.IsLoaded())
        return false;
    s.lastAccess = m_clock++;
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
        Seq* seq = ActiveSeqLocked();
        if (!seq || index < 0 || index >= static_cast<int>(seq->slots.size()))
            return;
        Slot& s = seq->slots[index];
        if (s.state != SlotState::Empty) // already Ready, or the worker is on it
            return;
        s.image = img;
        s.state = SlotState::Ready;
        s.lastAccess = m_clock++;
        m_bytes += FrameBytes(s.image);
        EvictLocked(m_activeId, index);
        inserted = true;
    }
    if (inserted)
        m_cv.notify_all();
}

void SequenceCache::SnapshotStates(std::vector<uint8_t>& out) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    const Seq* seq = ActiveSeqLocked();
    if (!seq)
    {
        out.clear();
        return;
    }
    out.resize(seq->slots.size());
    for (size_t i = 0; i < seq->slots.size(); ++i)
    {
        const Slot& s = seq->slots[i];
        if (s.state == SlotState::Ready && s.image.IsLoaded())
            out[i] = 2;
        else if (s.state == SlotState::Loading)
            out[i] = 1;
        else
            out[i] = 0;
    }
}

std::wstring SequenceCache::ActiveId() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_activeId;
}

long SequenceCache::FramePriorityLocked(int i, int n) const
{
    if (n == 0 || i == m_focus)
        return 0;
    int fwd = (i - m_focus + n) % n;  // steps forward (play direction), wrapping
    int back = (m_focus - i + n) % n; // steps backward
    if (m_reverseLookback > 0 && back <= m_reverseLookback)
        return back; // small reverse buffer for back-scrubbing (ping-pong)
    return static_cast<long>(m_reverseLookback) + fwd;
}

bool SequenceCache::PickNextLocked(std::wstring& outId, int& outIndex, std::wstring& outPath)
{
    Seq* seq = ActiveSeqLocked();
    if (!seq)
        return false;
    const int n = static_cast<int>(seq->slots.size());

    // Highest-priority (lowest value) Empty slot in the active sequence.
    int cand = -1;
    long candPri = 0;
    for (int i = 0; i < n; ++i)
    {
        if (seq->slots[i].state != SlotState::Empty)
            continue;
        long p = FramePriorityLocked(i, n);
        if (cand < 0 || p < candPri)
        {
            candPri = p;
            cand = i;
        }
    }
    if (cand < 0)
        return false; // nothing to decode

    if (m_bytes < m_budget)
    {
        outId = m_activeId;
        outIndex = cand;
        outPath = seq->paths[cand];
        return true;
    }

    // Over budget: load only if the candidate would be more valuable to keep than
    // the most-evictable resident frame (else it would just get evicted itself).
    long maxEvict = -1;
    for (const auto& kv : m_seqs)
    {
        const bool active = (kv.first == m_activeId);
        const Seq& s = kv.second;
        const int sn = static_cast<int>(s.slots.size());
        for (int i = 0; i < sn; ++i)
        {
            if (s.slots[i].state != SlotState::Ready)
                continue;
            long e = active ? FramePriorityLocked(i, sn)
                            : kInactiveEvictBase + static_cast<long>(m_clock - s.slots[i].lastAccess);
            if (e > maxEvict)
                maxEvict = e;
        }
    }
    if (candPri < maxEvict)
    {
        outId = m_activeId;
        outIndex = cand;
        outPath = seq->paths[cand];
        return true;
    }
    return false;
}

void SequenceCache::EvictLocked(const std::wstring& keepId, int keepIndex)
{
    while (m_bytes > m_budget)
    {
        // Drop the most-evictable resident frame: inactive sequences (LRU) before
        // active-sequence frames (kept by play-order priority).
        std::wstring victimId;
        int victimIndex = -1;
        long victimScore = -1;
        for (auto& kv : m_seqs)
        {
            const bool active = (kv.first == m_activeId);
            Seq& s = kv.second;
            const int sn = static_cast<int>(s.slots.size());
            for (int i = 0; i < sn; ++i)
            {
                if (s.slots[i].state != SlotState::Ready || !s.slots[i].image.IsLoaded())
                    continue;
                if (kv.first == keepId && i == keepIndex)
                    continue;
                long score = active ? FramePriorityLocked(i, sn)
                                    : kInactiveEvictBase + static_cast<long>(m_clock - s.slots[i].lastAccess);
                if (score > victimScore)
                {
                    victimScore = score;
                    victimId = kv.first;
                    victimIndex = i;
                }
            }
        }
        if (victimIndex < 0)
            break; // nothing left to evict

        Slot& v = m_seqs[victimId].slots[victimIndex];
        m_bytes -= FrameBytes(v.image);
        v.image = ImageData{};
        v.state = SlotState::Empty;
    }
}

void SequenceCache::WorkerLoop()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    while (!m_stop)
    {
        std::wstring seqId, path;
        int idx = -1;
        if (!PickNextLocked(seqId, idx, path))
        {
            m_cv.wait(lk);
            continue;
        }

        m_seqs[seqId].slots[idx].state = SlotState::Loading;

        lk.unlock();
        ImageData img;
        std::string err;
        bool ok = ImageLoader::LoadEXR(path, img, err);
        lk.lock();

        if (m_stop)
            continue;
        // The sequence may have been removed/rebuilt while we decoded — only store
        // if the slot we started on is still Loading.
        auto it = m_seqs.find(seqId);
        if (it == m_seqs.end() || idx >= static_cast<int>(it->second.slots.size()))
            continue;
        Slot& slot = it->second.slots[idx];
        if (slot.state != SlotState::Loading)
            continue;

        if (ok)
        {
            slot.image = std::move(img);
            slot.state = SlotState::Ready;
            slot.lastAccess = m_clock++;
            m_bytes += FrameBytes(slot.image);
            EvictLocked(seqId, idx);
        }
        else
        {
            // Mark Ready-but-empty so we don't spin re-decoding a bad frame;
            // TryGet treats it as a miss and the caller surfaces the error.
            slot.state = SlotState::Ready;
        }
        m_cv.notify_all();
    }
}
