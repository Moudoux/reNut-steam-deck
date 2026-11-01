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
// - The frame before last is kept too, for data the game may write to two
//   buffers in turn: the vertex fetch lookups were ~99% misses against last
//   frame alone, with every key one an earlier frame had.
//
// Predictions are always checked by the caller: a wrong one costs time, never
// a wrong result. T's == says whether two results are the same one.
template <typename T>
class FrameOrder {
public:
  static constexpr size_t kSeek = 32;

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

  // This lookup's result: each frame whose next kSeek items have it carries on
  // after it.
  void Record(const T &item) {
    bool found = false;
    bool at_point[2] = {};
    for (size_t k = 0; k < 2; ++k) {
      History &h = history_[k];
      const size_t end = std::min(h.items.size(), h.point + kSeek);
      for (size_t i = h.point; i < end; ++i) {
        if (h.items[i] == item) {
          found = true;
          at_point[k] = i == h.point;
          h.point = i + 1;
          break;
        }
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
    std::swap(history_[1].items, history_[0].items);
    std::swap(history_[0].items, cur_);
    cur_.clear();
    history_[0].point = history_[1].point = 0;
  }

  // Forgets every frame's items (what they point to may be gone).
  void Clear() {
    for (History &h : history_) {
      h.items.clear();
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
  };
  History history_[2];  // last frame's, the frame before's
  std::vector<T> cur_;  // this frame's so far
  FrameOrderStats stats_;
};

} // namespace renut::gpu
