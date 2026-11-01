/**
 * @file    gpu/render/frame_order.h
 * @brief   Last frame's lookups, in order, to predict this frame's.
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace renut::gpu {

struct FrameOrderStats {
  uint64_t hits = 0;     // last frame's item at the point
  uint64_t resyncs = 0;  // found further on, past a stretch this frame skipped
  uint64_t misses = 0;   // not among the next kSeek of last frame's
};

// The draws come in the same order frame after frame, so a lookup's result is
// most likely what the same point of last frame's sequence found, and that
// list is read in order where a hash table is a random line each. The point
// in last frame's sequence is kept apart from this frame's count: when the
// camera moves, objects come into view (lookups last frame did not have) and
// go out of it (lookups it had) mid-frame. Keeping one count for both, a
// single new object left every later prediction one off, and the rest of the
// frame missed (render thread 2.6 us per draw standing in the hub, 3.0-4.4
// moving, 2026-11-01). Now a lookup last frame did not have leaves the point
// where it is, and a skipped stretch is found again by a scan of the next
// kSeek items. Predictions are always checked by the caller: a wrong one
// costs time, never a wrong result.
template <typename T>
class FrameOrder {
public:
  static constexpr size_t kSeek = 32;

  // Last frame's item 'ahead' places past the point, or null past its end.
  const T *Peek(size_t ahead = 0) const {
    const size_t i = cursor_ + ahead;
    return i < prev_.size() ? &prev_[i] : nullptr;
  }

  // How far past the point the first of the next kSeek items 'match' accepts
  // is, if any.
  template <typename Match>
  std::optional<size_t> Seek(Match &&match) const {
    const size_t end = std::min(prev_.size(), cursor_ + kSeek);
    for (size_t i = cursor_; i < end; ++i) {
      if (match(prev_[i]))
        return i - cursor_;
    }
    return std::nullopt;
  }

  // This lookup is last frame's item 'ahead' places on, found as 'item' (that
  // item, or a fresher result for the same key): carry on after it.
  void Take(size_t ahead, const T &item) {
    Record(item);
    cursor_ += ahead + 1;
    ++(ahead ? stats_.resyncs : stats_.hits);
  }

  // A lookup last frame's sequence did not have here: the point stays.
  void Add(const T &item) {
    Record(item);
    ++stats_.misses;
  }

  // A new frame's lookups begin: this frame's sequence is next frame's
  // prediction.
  void NextFrame() {
    prev_.swap(cur_);
    cur_.clear();
    cursor_ = 0;
  }

  // Forgets both frames' items (the entries they point to may be gone).
  void Clear() {
    prev_.clear();
    cur_.clear();
    cursor_ = 0;
  }

  FrameOrderStats TakeStats() {
    const FrameOrderStats s = stats_;
    stats_ = {};
    return s;
  }

private:
  static constexpr size_t kMaxItems = 1u << 16;

  void Record(const T &item) {
    if (cur_.size() < kMaxItems)
      cur_.push_back(item);
  }

  std::vector<T> prev_;  // last frame's results, in order
  std::vector<T> cur_;   // this frame's so far
  size_t cursor_ = 0;    // the point in prev_ this frame's lookups have reached
  FrameOrderStats stats_;
};

} // namespace renut::gpu
