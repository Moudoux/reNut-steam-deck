/**
 * @file    gpu/backend/gpu_profile.cpp
 */
#include "renut_engine/gpu/backend/gpu_profile.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <plume_render_interface.h>
#include <rex/cvar.h>

#include "renut_engine/gpu/backend/backend.h"
#include "renut_engine/gpu/backend/video.h"
#include "renut_engine/gpu/log.h"

REXCVAR_DEFINE_DOUBLE(renut_gpu_profile, 0.0, "Nuts&Bolts/Debug",
                      "Every this many seconds, log the GPU's time per frame by kind of work "
                      "(draws per target, resolves, mip levels, present). 0 = off.");
REXCVAR_DEFINE_BOOL(renut_gpu_profile_draws, false, "Nuts&Bolts/Debug",
                    "With renut_gpu_profile: split draws by their shaders too (a timestamp per "
                    "draw, so the profile itself costs GPU time).");

namespace renut::gpu::gpu_profile {

namespace {

// Timestamps one frame may write; past them its work stays with the last
// kind marked. Per-draw marks need many more, but Vulkan has every query of the
// pool written each frame (EndList), so the pool is only that large then.
constexpr uint32_t kMaxMarks = 16384;
constexpr uint32_t kKindMarks = 2048;
constexpr uint32_t kEndOfList = 0;  // a list's last timestamp: no span follows
constexpr uint32_t kOther = 1;      // a list's start, before any kind is marked

struct Slot {
  std::unique_ptr<plume::RenderQueryPool> pool;
  bool open = false;    // the slot's frame has begun recording
  bool active = false;  // ... and is profiled
  uint32_t capacity = 0;  // the pool's queries
  uint32_t count = 0;
  uint32_t list = 0;
  uint32_t current = kEndOfList;
  std::array<uint32_t, kMaxMarks> labels{};
  std::array<uint32_t, kMaxMarks> lists{};
};
std::array<Slot, kFramesInFlight> g_slots;

std::vector<std::string> g_names = {"(list end)", "other"};
std::unordered_map<std::string, uint32_t> g_ids = {{"(list end)", kEndOfList}, {"other", kOther}};

// Since the last report: GPU nanoseconds and draws per kind, and frames.
std::vector<uint64_t> g_totals;
std::vector<uint64_t> g_draws;
uint64_t g_frames = 0;
std::chrono::steady_clock::time_point g_last_report = std::chrono::steady_clock::now();

void Write(VideoState &s, Slot &slot, uint32_t label) {
  s.command_list->writeTimestamp(slot.pool.get(), slot.count);
  slot.labels[slot.count] = label;
  slot.lists[slot.count] = slot.list;
  ++slot.count;
  slot.current = label;
}

void Report(double interval) {
  const auto now = std::chrono::steady_clock::now();
  if (std::chrono::duration<double>(now - g_last_report).count() < interval || !g_frames)
    return;
  g_last_report = now;
  std::vector<std::pair<uint64_t, uint32_t>> kinds;
  uint64_t total = 0;
  for (uint32_t i = 0; i < g_totals.size(); ++i) {
    if (g_totals[i]) {
      kinds.emplace_back(g_totals[i], i);
      total += g_totals[i];
    }
  }
  std::sort(kinds.rbegin(), kinds.rend());
  const double per_frame = 1e6 * double(g_frames);
  std::string lines;
  for (size_t i = 0; i < kinds.size() && i < 24; ++i) {
    const uint32_t id = kinds[i].second;
    const uint64_t draws = id < g_draws.size() ? g_draws[id] : 0;
    lines += fmt::format("\n  {:7.3f} ms  {}", double(kinds[i].first) / per_frame, g_names[id]);
    if (draws)
      lines += fmt::format("  ({:.1f} draws)", double(draws) / double(g_frames));
  }
  RGPU_INFO("GPU profile: {:.3f} ms per frame over {} frames{}", double(total) / per_frame,
            g_frames, lines);
  std::fill(g_totals.begin(), g_totals.end(), 0);
  std::fill(g_draws.begin(), g_draws.end(), 0);
  g_frames = 0;
}

} // namespace

uint32_t Label(std::string_view name) {
  auto it = g_ids.find(std::string(name));
  if (it != g_ids.end())
    return it->second;
  const uint32_t id = uint32_t(g_names.size());
  g_names.emplace_back(name);
  g_ids.emplace(std::string(name), id);
  return id;
}

void Mark(VideoState &s, uint32_t label) {
  Slot &slot = g_slots[s.frame];
  if (!slot.active || !s.command_list || label == slot.current || slot.count + 2 >= slot.capacity)
    return;
  Write(s, slot, label);
}

bool DrawsActive(VideoState &s) {
  return REXCVAR_GET(renut_gpu_profile_draws) && g_slots[s.frame].active;
}

void MarkDraw(VideoState &s, uint32_t label) {
  Slot &slot = g_slots[s.frame];
  if (!slot.active)
    return;
  Mark(s, label);
  if (g_draws.size() <= label)
    g_draws.resize(g_names.size());
  ++g_draws[label];
}

void BeginList(VideoState &s) {
  Slot &slot = g_slots[s.frame];
  if (!slot.open) {
    slot.open = true;
    slot.count = 0;
    slot.list = 0;
    slot.active = REXCVAR_GET(renut_gpu_profile) > 0.0 && s.device->getCapabilities().queryPools;
    const uint32_t capacity = REXCVAR_GET(renut_gpu_profile_draws) ? kMaxMarks : kKindMarks;
    if (slot.active && (!slot.pool || slot.capacity != capacity)) {
      slot.pool = s.device->createQueryPool(capacity);
      slot.capacity = slot.pool ? capacity : 0;
      slot.active = slot.pool != nullptr;
    }
    // Vulkan queries are reset before they are written again.
    if (slot.active)
      s.command_list->resetQueryPool(slot.pool.get(), 0, slot.capacity);
  }
  if (slot.active && slot.count + 2 < slot.capacity)
    Write(s, slot, kOther);
}

void EndList(VideoState &s, bool last_in_frame) {
  Slot &slot = g_slots[s.frame];
  if (!slot.active || !s.command_list)
    return;
  if (slot.count < slot.capacity)
    Write(s, slot, kEndOfList);
  ++slot.list;
  // Vulkan returns no results while any query of the pool is unwritten.
  if (last_in_frame && IsVulkan()) {
    for (uint32_t i = slot.count; i < slot.capacity; ++i)
      s.command_list->writeTimestamp(slot.pool.get(), i);
  }
}

void Collect(VideoState &s, uint32_t index) {
  (void)s;
  Slot &slot = g_slots[index];
  const bool active = slot.active && slot.count > 1;
  slot.open = false;
  slot.active = false;
  if (!active)
    return;
  slot.pool->queryResults();
  const uint64_t *ns = slot.pool->getResults();
  if (g_totals.size() < g_names.size())
    g_totals.resize(g_names.size());
  for (uint32_t i = 0; i + 1 < slot.count; ++i) {
    if (slot.labels[i] == kEndOfList || slot.lists[i] != slot.lists[i + 1] || ns[i + 1] <= ns[i])
      continue;
    g_totals[slot.labels[i]] += ns[i + 1] - ns[i];
  }
  ++g_frames;
  Report(REXCVAR_GET(renut_gpu_profile));
}

} // namespace renut::gpu::gpu_profile
