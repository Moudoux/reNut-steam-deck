#pragma once

#include <cstdint>
#include <memory>

#include <plume_render_interface.h>

#include "shader_cache.h"

namespace renut::gpu {

// XXH3_64 of a whole guest shader container (virtual + physical), the key the
// cache and tools/dump_shaders.py use.
uint64_t HashShaderContainer(const uint8_t *container);

const ShaderCacheEntry *FindShaderEntry(uint64_t hash);

// reNut's own shaders for NutTool's models: a container whose physical part starts with "RNUTPART"
// and its kind (1 vertex, 2 pixel) runs them. nullptr for any other container.
const ShaderCacheEntry *BuiltInShaderEntry(const uint8_t *container, bool pixel_shader);

// Whether it is one of the built-in vertex shaders that blend NutTool's shape keys (part, world, skinned).
bool IsShapedVertexShader(const ShaderCacheEntry &entry);

// Whether it is the built-in part vertex shader (blocks of their own, whose pixel c28.w names the part).
bool IsPartVertexShader(const ShaderCacheEntry &entry);

// Whether it is the built-in world pixel shader (worlds and characters), which takes their materials.
bool IsWorldPixelShader(const ShaderCacheEntry &entry);

// A shader's vertex inputs (a built-in shader's own, else the cache's).
const ShaderVertexInput *ShaderVertexInputs(const ShaderCacheEntry &entry);

// Builds the active backend's shader from a cache entry (decompressing the
// cache on first use). nullptr on failure.
std::unique_ptr<plume::RenderShader> CreateCachedShader(plume::RenderDevice *device,
                                                        const ShaderCacheEntry &entry);

// Host shaders made ahead of their first draw: the game makes its shader
// objects at a load, and each was decoded and created at its first draw, on
// the render thread (smolv::Decode in the frames where something came into
// view: most of what walking about the hub cost over standing, perf on a
// Steam Deck, 2026-11-01). PrepareCachedShader queues an entry for a thread
// of its own (any thread); TakeCachedShader gives the shader it made, waits
// for one it is making, or makes it there and then as CreateCachedShader does
// (a queued one is taken off the queue). The game has many shader objects of
// one cache entry: they all share its host shader, which lives as long as any
// of them does. Each making its own was 2,600 decodes in the hub's first frame
// (191 ms of its 256, Steam Deck, 2026-11-02). StopShaderPreparer, at shutdown,
// frees what was made and not taken.
void PrepareCachedShader(plume::RenderDevice *device, const ShaderCacheEntry *entry);
std::shared_ptr<plume::RenderShader> TakeCachedShader(plume::RenderDevice *device,
                                                      const ShaderCacheEntry &entry);
void StopShaderPreparer();

// Writes a shader FindShaderEntry did not find to renut_shader_capture_dir,
// once per hash, for the next build to recompile into the cache.
void CaptureMissingShader(const uint8_t *container, bool pixel_shader, uint64_t hash);

} // namespace renut::gpu
