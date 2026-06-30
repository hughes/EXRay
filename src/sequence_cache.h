// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "image.h"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Background decoder that keeps a window of decoded sequence frames resident in
// RAM, so navigating (or playing) a sequence is just a GPU upload rather than a
// disk read + EXR decompress. A single worker thread decodes frames outward from
// the focused frame until the whole sequence is cached or the memory budget is
// reached, at which point it maintains a sliding window around the focus.
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
    // `paths` are the frame file paths in display order.
    void Configure(const std::wstring& id, std::vector<std::wstring> paths, size_t memoryBudgetBytes);

    // Stop the worker and release all cached frames.
    void Clear();

    // Tell the cache which frame is being viewed. Drives prefetch order and the
    // eviction window; wakes the worker.
    void SetFocus(int index);

    // Non-blocking. If frame `index` is decoded and resident, copies it into
    // `out` and returns true. Otherwise returns false (caller decodes itself).
    bool TryGet(int index, ImageData& out);

    // Insert a frame the caller decoded synchronously (cold-path fallback) so it
    // is not decoded again. Ignored if the slot is already filled or in flight.
    void Put(int index, const ImageData& img);

    const std::wstring& Id() const { return m_id; }
    int ReadyCount() const;

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
    int PickNextLocked() const;          // next Empty slot worth decoding, or -1
    void EvictLocked(int keepIndex);     // drop farthest Ready frames over budget
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
    size_t m_budget = 0;
    size_t m_bytes = 0; // total bytes of Ready slots
};
