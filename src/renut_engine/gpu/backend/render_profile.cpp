/**
 * @file    gpu/backend/render_profile.cpp
 */
#include "renut_engine/gpu/backend/render_profile.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include <fmt/format.h>
#include <rex/cvar.h>

#include "renut_engine/gpu/backend/command_recorder.h"
#include "renut_engine/gpu/log.h"

REXCVAR_DEFINE_DOUBLE(renut_render_profile, 0.0, "Nuts&Bolts/Debug",
                      "Every this many seconds, log the render thread's CPU time per frame by "
                      "stage of the work (what the overlay's Render is spent on). 0: off.");
REXCVAR_DEFINE_BOOL(renut_render_profile_stages, true, "Nuts&Bolts/Debug",
                    "With renut_render_profile: time each stage of every draw. Off logs only the "
                    "busy time and draws per frame, which costs next to nothing (for comparing "
                    "builds).");

namespace renut::gpu::render_profile {

namespace {

using Clock = std::chrono::steady_clock;

constexpr const char *kStageNames[] = {
    "shaders, declaration and targets found",
    "waiting for memexport results",
    "textures (bound, uploaded or converted)",
    "geometry (indices read, expanded, cached)",
    "vertex fetch (memexport streams, fetch data)",
    "constants uploaded",
    "pipelines (looked up, built when new)",
    "render targets bound (render passes)",
    "vertex streams (converted, bound)",
    "draw state and draw recorded",
    "clears",
    "resolves",
};
static_assert(std::size(kStageNames) == size_t(Stage::kCount));

// Since the last report.
std::array<Clock::duration, size_t(Stage::kCount)> g_stages{};
uint64_t g_draws = 0;
uint64_t g_frames = 0;
// This frame's, for the spike log.
uint32_t g_frame_draws = 0, g_frame_builds = 0, g_frame_uploads = 0;
uint32_t g_spikes_logged = 0;
Clock::time_point g_spike_window = Clock::now();
double g_busy_ms = 0.0;
double g_recorder_ms = 0.0;
Clock::time_point g_since = Clock::now();

void Report() {
  const double frames = double(std::max<uint64_t>(g_frames, 1));
  std::array<std::pair<double, size_t>, size_t(Stage::kCount)> stages{};
  double counted = 0.0;
  for (size_t i = 0; i < stages.size(); ++i) {
    const double ms = std::chrono::duration<double, std::milli>(g_stages[i]).count() / frames;
    stages[i] = {ms, i};
    counted += ms;
  }
  std::sort(stages.begin(), stages.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
  const double busy = g_busy_ms / frames;
  std::string lines;
  for (const auto &[ms, i] : stages) {
    if (ms >= 0.005)
      lines += fmt::format("\n  {:7.3f} ms  {}", ms, kStageNames[i]);
  }
  if (counted > 0.0)
    lines += fmt::format("\n  {:7.3f} ms  the rest (present, uploads, other work)", std::max(0.0, busy - counted));
  // Beside it, not part of it (renut_threaded_recording).
  if (g_recorder_ms > 0.0)
    lines += fmt::format("\n  recording thread: {:.3f} ms per frame of host API calls", g_recorder_ms / frames);
  const double draws = double(g_draws) / frames;
  RGPU_INFO("Render thread profile: {:.3f} ms busy per frame over {} frames, {:.0f} draws per frame "
            "({:.2f} us per draw){}",
            busy, g_frames, draws, draws > 0.0 ? 1000.0 * busy / draws : 0.0, lines);
  g_stages = {};
  g_draws = 0;
  g_frames = 0;
  g_busy_ms = 0.0;
  g_recorder_ms = 0.0;
}

}  // namespace

bool Enabled() {
  return REXCVAR_GET(renut_render_profile) > 0.0;
}

namespace {
bool StagesEnabled() {
  return Enabled() && REXCVAR_GET(renut_render_profile_stages);
}
}  // namespace

Scope::Scope(Stage stage) : stage_(stage), active_(StagesEnabled()) {
  if (active_)
    start_ = Clock::now();
}

Scope::~Scope() {
  if (active_)
    g_stages[size_t(stage_)] += Clock::now() - start_;
}

Stages::Stages() : active_(StagesEnabled()) {}

Stages::~Stages() {
  if (timing_)
    End(Clock::now());
}

void Stages::End(Clock::time_point now) {
  g_stages[size_t(stage_)] += now - start_;
  timing_ = false;
}

void Stages::Enter(Stage stage) {
  if (!active_)
    return;
  const Clock::time_point now = Clock::now();
  if (timing_)
    End(now);
  stage_ = stage;
  start_ = now;
  timing_ = true;
}

void CountDraw() {
  if (Enabled()) {
    ++g_draws;
    ++g_frame_draws;
  }
}

void CountPipelineBuild() {
  if (Enabled())
    ++g_frame_builds;
}

void CountTextureUpload() {
  if (Enabled())
    ++g_frame_uploads;
}

void FrameDone(double busy_ms) {
  const double interval = REXCVAR_GET(renut_render_profile);
  const double recorder_ms = CommandRecorder::TakeBusyMs();
  if (interval <= 0.0) {
    g_since = Clock::now();
    return;
  }
  ++g_frames;
  g_busy_ms += busy_ms;
  // A frame the render thread could not finish within a 60 Hz refresh, with
  // what it did that a usual frame does not (at most 30 a minute).
  constexpr double kSpikeMs = 1000.0 / 60.0;
  if (busy_ms > kSpikeMs) {
    const Clock::time_point now = Clock::now();
    if (now - g_spike_window > std::chrono::minutes(1)) {
      g_spike_window = now;
      g_spikes_logged = 0;
    }
    if (g_spikes_logged++ < 30) {
      RGPU_INFO("Render thread spike: {:.1f} ms, {} draws, {} pipelines built, {} textures uploaded",
                busy_ms, g_frame_draws, g_frame_builds, g_frame_uploads);
    }
  }
  g_frame_draws = g_frame_builds = g_frame_uploads = 0;
  g_recorder_ms += recorder_ms;
  if (Clock::now() - g_since >= std::chrono::duration<double>(interval)) {
    Report();
    g_since = Clock::now();
  }
}

}  // namespace renut::gpu::render_profile
