/**
 * @file    gpu/render/frame_order.h
 * @brief   The last frames' lookups, in order, to predict this frame's.
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace renut::gpu {

struct FrameOrderStats {
  uint64_t hits = 0;     // last frame's item at its point
  uint64_t older = 0;    // the frame before's at its point (not last frame's)
  uint64_t resyncs = 0;  // found further on, past a stretch this frame skipped
  uint64_t misses = 0;   // in neither frame's next kSeek
  // Of the lookups not predicted, for callers that count them:
  uint64_t made = 0;    // a new entry made
  uint64_t absent = 0;  // nothing found (and none made)
};

// The draws come in the same order frame after frame, so a lookup's result is
// most likely what the same point of last frame's sequence found, and that
// list is read in order where a hash table is a random line each.
//
// - Each frame's point is kept apart from this frame's count: when the camera
//   moves, objects come into view (lookups last frame did not have) and go out
//   of it (lookups it had) mid-frame. With one count for both, a single new
//   object left every later prediction one off, and the rest of the frame
//   missed (render thread 2.6 us per draw standing in the hub, 3.0-4.4
//   moving, 2026-11-01). A result a frame did not have leaves its point where
//   it is; one found within its next kSeek items moves the point past it.
// - Once kStuck results in a row are not in a frame's next kSeek, the next is
//   looked for by an index of that frame's items (made then), and the point
//   moves past it: a stretch of any length this frame has elsewhere, or not at
//   all, no longer leaves the point behind it for the rest of the frame. (Not
//   sooner: an object come into view misses a few, and the index costs a pass
//   over the frame's items.) (The vertex fetch lookups take
//   a batch at each flush, and the flushes fall at other draws from frame to
//   frame: ~99% of them missed, every key one the frame before had.)
// - The frame before last is kept too: moving, 1.5-2.3% of the lookups are at
//   its point and not at last frame's.
//
// Predictions are always checked by the caller: a wrong one costs time, never
// a wrong result. T's == says whether two results are the same one; Key gives
// a result's 64-bit identity for the index.
struct FrameOrderPointerKey {
  uint64_t operator()(const void *p) const { return uint64_t(reinterpret_cast<uintptr_t>(p)); }
};

template <typename T, typename Key = FrameOrderPointerKey>
class FrameOrder {
public:
  static constexpr size_t kSeek = 32;
  static constexpr uint32_t kStuck = 8;

  // The first item 'match' accepts at the frames' points or less than 'reach'
  // past them, last frame's first; null if none.
  template <typename Match>
  const T *Find(size_t reach, Match &&match) const {
    for (const History &h : history_) {
      const size_t end = std::min(h.items.size(), h.point + reach);
      for (size_t i = h.point; i < end; ++i) {
        if (match(h.items[i]))
          return &h.items[i];
      }
    }
    return nullptr;
  }

  // This lookup's result: each frame that has it at or after its point
  // carries on after it (the first such in the next kSeek, else where the
  // index has it).
  void Record(const T &item) {
    bool found = false;
    bool at_point[2] = {};
    for (size_t k = 0; k < 2; ++k) {
      History &h = history_[k];
      const size_t end = std::min(h.items.size(), h.point + kSeek);
      size_t i = h.point;
      while (i < end && !(h.items[i] == item))
        ++i;
      if (i == end)
        i = end < h.items.size() && h.missed >= kStuck ? h.Indexed(item, end) : h.items.size();
      if (i < h.items.size()) {
        found = true;
        at_point[k] = i == h.point;
        h.point = i + 1;
        h.missed = 0;
      } else {
        ++h.missed;
      }
    }
    if (at_point[0])
      ++stats_.hits;
    else if (at_point[1])
      ++stats_.older;
    else if (found)
      ++stats_.resyncs;
    else
      ++stats_.misses;
    if (cur_.size() < kMaxItems)
      cur_.push_back(item);
  }

  // A new frame's lookups begin: this frame's are the last frame's.
  void NextFrame() {
    std::swap(history_[1], history_[0]);
    std::swap(history_[0].items, cur_);
    history_[0].indexed = false;
    cur_.clear();
    for (History &h : history_) {
      h.point = 0;
      h.missed = 0;
    }
  }

  // Forgets every frame's items (what they point to may be gone).
  void Clear() {
    for (History &h : history_) {
      h.items.clear();
      h.indexed = false;
      h.point = 0;
    }
    cur_.clear();
  }

  FrameOrderStats TakeStats() { return std::exchange(stats_, {}); }

private:
  static constexpr size_t kMaxItems = 1u << 16;

  struct History {
    std::vector<T> items;  // that frame's results, in order
    size_t point = 0;      // how far this frame's lookups have come in them
    uint32_t missed = 0;   // results in a row not found
    // Open addressing over items: each result's first position + 1 (0 empty).
    std::vector<uint32_t> index;
    uint32_t shift = 64;
    bool indexed = false;

    // Where 'item' is at or after 'from', by the index (made at the first
    // call); items.size() if not (or only before 'from': a result drawn more
    // than once is indexed at its first).
    size_t Indexed(const T &item, size_t from) {
      if (!indexed) {
        indexed = true;
        uint32_t bits = 4;
        while ((size_t(1) << bits) < items.size() * 2)
          ++bits;
        shift = 64 - bits;
        index.assign(size_t(1) << bits, 0);
        for (size_t i = 0; i < items.size(); ++i) {
          for (size_t slot = Slot(items[i]);; slot = (slot + 1) & (index.size() - 1)) {
            if (!index[slot]) {
              index[slot] = uint32_t(i + 1);
              break;
            }
            if (items[index[slot] - 1] == items[i])
              break;  // the first is kept
          }
        }
      }
      for (size_t slot = Slot(item); index[slot]; slot = (slot + 1) & (index.size() - 1)) {
        const size_t i = index[slot] - 1;
        if (items[i] == item)
          return i >= from ? i : items.size();
      }
      return items.size();
    }

    size_t Slot(const T &item) const {
      return size_t((Key{}(item) * 0x9E3779B97F4A7C15ull) >> shift);
    }
  };
  History history_[2];  // last frame's, the frame before's
  std::vector<T> cur_;  // this frame's so far
  FrameOrderStats stats_;
};

} // namespace renut::gpu
