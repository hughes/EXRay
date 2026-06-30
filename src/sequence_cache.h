// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "image.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Background decoder that keeps decoded sequence frames resident in RAM so
// navigation/playback is a GPU upload rather than a disk decode.
//
// Holds frames from MULTIPLE sequences at once under a single global byte
// budget: switching tabs keeps each sequence's frames warm (no reload) until
// memory pressure evicts the least-recently-used frames. One background worker
// prefetches the ACTIVE sequence in play order (forward-wrap from the focus);
// inactive sequences simply retain what they already decoded.
class SequenceCache
{
  public:
    SequenceCache() = default;
    ~SequenceCache();

    SequenceCache(const SequenceCache&) = delete;
    SequenceCache& operator=(const SequenceCache&) = delete;

    // Global memory budget in bytes (total across all cached sequences).
    void SetBudget(size_t bytes);

    // Make sequence `id` active (creates its entry if new, KEEPS any frames it
    // already has). `paths` are the frame file paths in display order. If `layer`
    // is non-null the worker decodes that layer (e.g. Z depth) for each frame
    // instead of the default RGBA; `id` should encode the layer so different
    // layers get separate cache entries. The worker prefetches this sequence;
    // TryGet/Put/SnapshotStates/SetFocus target it.
    void Activate(const std::wstring& id, std::vector<std::wstring> paths, const ExrLayer* layer = nullptr);

    // No active sequence (e.g. a still-image tab): stop prefetching but keep all
    // resident frames for when a sequence becomes active again.
    void Deactivate();

    // Drop a sequence's frames entirely (its tab was closed).
    void Remove(const std::wstring& id);

    // Drop every cached entry for a sequence — the default-RGBA entry and all of
    // its per-layer entries (`baseId` and `baseId|L…`). Used on reload so changed
    // files are re-decoded rather than served stale.
    void RemoveSequence(const std::wstring& baseId);

    // Stop the worker and release everything.
    void Clear();

    // Drive prefetch order / eviction window for the active sequence.
    void SetFocus(int index);
    // Frames behind focus to keep warm (0 = forward-only; >0 for ping-pong).
    void SetReverseLookback(int frames);

    // Active sequence, non-blocking: copy frame `index` into `out` if resident.
    bool TryGet(int index, ImageData& out);
    // Seed a frame the caller decoded synchronously into the active sequence.
    void Put(int index, const ImageData& img);
    // Active sequence residency snapshot: 0=not loaded,1=decoding,2=resident.
    void SnapshotStates(std::vector<uint8_t>& out) const;

    // Id of the active sequence ("" if none). Main-thread use.
    std::wstring ActiveId() const;

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
        uint64_t lastAccess = 0; // monotonic, for LRU eviction
    };

    struct Seq
    {
        std::vector<std::wstring> paths;
        std::vector<Slot> slots;
        bool useLayer = false; // decode `layer` instead of default RGBA
        ExrLayer layer;
    };

    void StartWorker();
    void StopWorker();
    void WorkerLoop();

    // All of the following must be called with m_mutex held.
    Seq* ActiveSeqLocked();
    const Seq* ActiveSeqLocked() const;
    // Play-order priority within the active sequence (lower = sooner): focus first,
    // small reverse lookback, then forward wrapping past the end to frame 0.
    long FramePriorityLocked(int i, int n) const;
    // Next frame the worker should decode: fills the active sequence's id/index/path,
    // or returns false if there is nothing worth decoding right now.
    bool PickNextLocked(std::wstring& outId, int& outIndex, std::wstring& outPath);
    // Evict highest-score Ready frames until total resident bytes fit the budget.
    // Inactive frames (older lastAccess = higher score) are dropped before active
    // frames (kept by play-order priority). `keepId`/`keepIndex` is never evicted.
    void EvictLocked(const std::wstring& keepId, int keepIndex);
    static size_t FrameBytes(const ImageData& img);

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_worker;
    bool m_stop = false;
    bool m_workerStarted = false;

    std::unordered_map<std::wstring, Seq> m_seqs;
    std::wstring m_activeId; // "" = none active
    int m_focus = 0;
    int m_reverseLookback = 0;
    size_t m_budget = 0;
    size_t m_bytes = 0;   // total resident bytes across all sequences
    uint64_t m_clock = 1; // monotonic access counter for LRU

    // Inactive frames always outrank active frames for eviction (drop inactive
    // first). Larger than any active play-order priority (< ~2 * frame count).
    static constexpr long kInactiveEvictBase = 1L << 30;
};
