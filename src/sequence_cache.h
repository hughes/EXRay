// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "image.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Background decoder that keeps a window of decoded sequence frames resident in
// RAM, so navigating (or playing) a sequence is just a GPU upload rather than a
// disk read + EXR decompress. A single worker thread decodes frames outward from
// the focused frame (forward-wrap play order) until the whole sequence is cached
// or the memory budget is reached, then maintains a window around the focus.
class SequenceCache
{
  public:
    SequenceCache() = default;
    ~SequenceCache();

    SequenceCache(const SequenceCache&) = delete;
    SequenceCache& operator=(const SequenceCache&) = delete;

    // (Re)configure for a sequence identified by `id` (e.g. prefix+suffix). If
    // `id` matches the current configuration this is a no-op, preserving the
    // already-warm cache. Otherwise the cache resets and prefetch restarts.
    void Configure(const std::wstring& id, std::vector<std::wstring> paths, size_t memoryBudgetBytes);

    // Stop the worker and release all cached frames.
    void Clear();

    // Drive prefetch order / eviction window.
    void SetFocus(int index);
    // Frames behind focus to keep warm (0 = forward-only; >0 for ping-pong).
    void SetReverseLookback(int frames);

    // Non-blocking: copy frame `index` into `out` if resident.
    bool TryGet(int index, ImageData& out);
    // Seed a frame the caller decoded synchronously.
    void Put(int index, const ImageData& img);

    const std::wstring& Id() const { return m_id; }
    int ReadyCount() const;
    // Per-frame residency snapshot: 0 = not loaded, 1 = decoding, 2 = resident.
    void SnapshotStates(std::vector<uint8_t>& out) const;

  private:
    enum class SlotState
    {
        Empty,
        Loading,
        Ready
    };

    struct Slot
    {
        SlotState state = SlotState::Empty;
        ImageData image;
    };

    void StartWorker();
    void StopWorker();
    void WorkerLoop();

    // All of the following must be called with m_mutex held.
    long FramePriorityLocked(int i) const;
    int PickNextLocked() const;
    void EvictLocked(int keepIndex);
    static size_t FrameBytes(const ImageData& img);

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_worker;
    bool m_stop = false;
    uint64_t m_generation = 0; // bumped on Configure/Clear to invalidate in-flight decodes

    std::wstring m_id;
    std::vector<std::wstring> m_paths;
    std::vector<Slot> m_slots;
    int m_focus = 0;
    int m_reverseLookback = 0; // frames behind focus to keep warm (ping-pong only)
    size_t m_budget = 0;
    size_t m_bytes = 0; // total bytes of Ready slots
};
