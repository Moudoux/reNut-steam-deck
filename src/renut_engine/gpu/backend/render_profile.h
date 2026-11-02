/**
 * @file    gpu/backend/render_profile.h
 * @brief   Where the render thread's CPU time goes (renut_render_profile).
 *
 * The overlay's "Render" is the render thread's busy time per frame (its waits
 * for the GPU and the display left out): the CPU work of turning the game's
 * D3D calls into the host API's. With renut_render_profile above 0 that time
 * is split into the stages of a draw (shaders and targets, textures,
 * geometry, vertex fetch, constants, pipelines, render passes, vertex
 * streams, recording) and the work between draws (clears, resolves), and
 * every renut_render_profile seconds the log lists them by milliseconds per
 * frame, with the draws per frame. Off, a scope costs one flag test. Render
 * thread only.
 */
#pragma once

#include <chrono>
#include <cstdint>

namespace renut::gpu::render_profile {

enum class Stage : uint8_t {
  kShaders,      // the draw's shaders, declaration and targets found
  kExportWait,   // waiting for earlier memexport results
  kTextures,     // textures bound (and uploaded or converted when they changed)
  kGeometry,     // indices read, expanded or cached
  kVertexFetch,  // memexport streams converted, vertex fetch data uploaded
  kConstants,    // float, bool and shared constants uploaded
  kPipeline,     // pipeline looked up (and built when new)
  kTargets,      // render targets bound (render pass changes)
  kStreams,      // vertex streams converted and bound
  kRecord,       // the draw's state and draw recorded
  kClear,        // clears between draws
  kResolve,      // resolves between draws
  kCount
};

bool Enabled();

// Times its scope into 'stage' (when the profile is on).
class Scope {
 public:
  explicit Scope(Stage stage);
  ~Scope();
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;

 private:
  Stage stage_;
  bool active_;
  std::chrono::steady_clock::time_point start_;
};

// Times the consecutive stages of one piece of work (a draw): each Enter ends
// the stage before it, and the last ends with the object.
class Stages {
 public:
  Stages();
  ~Stages();
  Stages(const Stages &) = delete;
  Stages &operator=(const Stages &) = delete;
  void Enter(Stage stage);

 private:
  void End(std::chrono::steady_clock::time_point now);
  bool active_;
  bool timing_ = false;
  Stage stage_ = Stage::kShaders;
  std::chrono::steady_clock::time_point start_;
};

// A draw was recorded.
void CountDraw();

// For the spike log: a pipeline built on the render thread, a texture uploaded
// ('async': its guest data read on the texture upload thread).
void CountPipelineBuild();
void CountTextureUpload(bool async = false);
// For the spike log: milliseconds the render thread spent finishing the
// texture upload thread's reads at a submission (UploadThread::Finish).
void AddTextureFillWait(double ms);
// For the spike log: milliseconds spent creating a host texture (Create in
// textures.cpp), and vertex streams converted at a flush.
void AddTextureCreate(double ms);
void CountStreamConversions(uint32_t count);

// For the geometry trace ("Geometry frame:" lines): what this frame's draws did
// about geometry no cache had a copy of, filled in by draw.cpp (render thread).
struct GeometryFrame {
  uint32_t streams_first = 0, streams_again = 0, streams_other = 0;  // converted
  uint64_t stream_bytes = 0;                                         // guest bytes of those
  uint32_t flushes = 0;
  double flush_ms = 0.0, convert_ms = 0.0, track_ms = 0.0;  // convert: the parallel part
  uint32_t index_reads = 0;
  uint64_t index_bytes = 0;
  double index_ms = 0.0;
  uint32_t fetch_copies = 0;
  uint64_t fetch_bytes = 0;
  uint32_t pool_copies = 0;
  uint64_t pool_bytes = 0;
  uint32_t draws_left_out = 0;
  uint32_t wall_requests = 0, wall_shown = 0, wall_opening = 0;
  // ShaderPreparer::Take: made ahead, sharing another record's, waited for, made
  // on the render thread.
  uint32_t take_ready = 0, take_shared = 0, take_wait = 0, take_inline = 0;
  double take_wait_ms = 0.0, take_inline_ms = 0.0;
  uint32_t chunks = 0;  // pool chunks taken (a spare, or made here)
  double chunk_ms = 0.0;
  uint32_t resumed = 0, resume_stale = 0;  // FreshAfterIdle's hashes
  double resume_ms = 0.0;
};
GeometryFrame &Geometry();

// The render thread finished a frame, busy for 'busy_ms' (its waits left out):
// adds it, and logs the report when it is due.
void FrameDone(double busy_ms);

}  // namespace renut::gpu::render_profile
