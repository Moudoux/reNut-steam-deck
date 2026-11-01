/**
 * @file    gpu/render/shader_library.cpp
 */
#include "renut_engine/gpu/render/shader_library.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <smolv.h>
#include <xxhash.h>
#include <zstd.h>

#if defined(__linux__)
#include <pthread.h>
#endif

#include "renut_engine/gpu/backend/backend.h"
#include "renut_engine/gpu/d3d/d3d.h"
#include "renut_engine/gpu/log.h"

REXCVAR_DEFINE_STRING(renut_shader_capture_dir, "shader_capture", "Nuts&Bolts/Graphics",
                      "Where shaders missing from the shader cache are written; the next build "
                      "recompiles them into the cache");

namespace renut::gpu {

namespace {

struct DecompressedCache {
  std::once_flag once;
  std::vector<uint8_t> bytes;
};

DecompressedCache g_spirv;
#if defined(_WIN32)
DecompressedCache g_dxil;
#endif
std::atomic<uint64_t> g_decompressed_bytes{0};  // ShaderCacheHostBytes

const uint8_t *Decompress(DecompressedCache &cache, const uint8_t *compressed,
                          size_t compressed_size, size_t decompressed_size, const char *name) {
  std::call_once(cache.once, [&] {
    cache.bytes.resize(decompressed_size);
    const size_t n = ZSTD_decompress(cache.bytes.data(), decompressed_size, compressed,
                                     compressed_size);
    if (ZSTD_isError(n) || n != decompressed_size) {
      RGPU_ERROR("{} shader cache decompression failed", name);
      cache.bytes.clear();
      cache.bytes.shrink_to_fit();
    }
    g_decompressed_bytes.fetch_add(cache.bytes.capacity(), std::memory_order_relaxed);
  });
  return cache.bytes.empty() ? nullptr : cache.bytes.data();
}

std::mutex g_capture_mutex;
std::unordered_set<uint64_t> g_captured;  // CaptureMissingShader

} // namespace

uint64_t HashShaderContainer(const uint8_t *container) {
  const auto *header = reinterpret_cast<const d3d::ShaderContainer *>(container);
  const size_t size = size_t(u32(header->virtualSize)) + u32(header->physicalSize);
  return XXH3_64bits(container, size);
}

const ShaderCacheEntry *FindShaderEntry(uint64_t hash) {
  if (g_shaderCacheLayout != kShaderCacheLayout) {
    static bool logged = false;
    if (!logged) {
      RGPU_ERROR("Shader cache layout {} does not match the renderer's {}; rebuild it",
                 g_shaderCacheLayout, kShaderCacheLayout);
      logged = true;
    }
    return nullptr;
  }
  const ShaderCacheEntry *begin = g_shaderCacheEntries;
  const ShaderCacheEntry *end = begin + g_shaderCacheEntryCount;
  const ShaderCacheEntry *it = std::lower_bound(
      begin, end, hash, [](const ShaderCacheEntry &e, uint64_t h) { return e.hash < h; });
  return (it != end && it->hash == hash) ? it : nullptr;
}

std::unique_ptr<plume::RenderShader> CreateCachedShader(plume::RenderDevice *device,
                                                        const ShaderCacheEntry &entry) {
  if (!device)
    return nullptr;

  if (IsVulkan()) {
    const uint8_t *cache = Decompress(g_spirv, g_compressedSpirvCache, g_spirvCacheCompressedSize,
                                      g_spirvCacheDecompressedSize, "SPIR-V");
    if (!cache || entry.spirvSize == 0)
      return nullptr;
    const uint8_t *smol = cache + entry.spirvOffset;
    std::vector<uint8_t> spirv(smolv::GetDecodedBufferSize(smol, entry.spirvSize));
    if (spirv.empty() || !smolv::Decode(smol, entry.spirvSize, spirv.data(), spirv.size())) {
      RGPU_ERROR("SPIR-V decode failed for shader {:016X}", entry.hash);
      return nullptr;
    }
    return device->createShader(spirv.data(), spirv.size(), "main",
                                plume::RenderShaderFormat::SPIRV);
  }

#if defined(_WIN32)
  const uint8_t *cache = Decompress(g_dxil, g_compressedDxilCache, g_dxilCacheCompressedSize,
                                    g_dxilCacheDecompressedSize, "DXIL");
  if (!cache || entry.dxilSize == 0)
    return nullptr;
  return device->createShader(cache + entry.dxilOffset, entry.dxilSize, "main",
                              plume::RenderShaderFormat::DXIL);
#else
  return nullptr;
#endif
}

namespace {

// PrepareCachedShader's thread. One, for the 15 W budget the CPU shares with
// the GPU; it sleeps when nothing is queued.
class ShaderPreparer {
public:
  static ShaderPreparer &Get() {
    // Never destroyed: Stop frees the shaders while the device exists.
    static ShaderPreparer *preparer = new ShaderPreparer;
    return *preparer;
  }

  void Prepare(plume::RenderDevice *device, const ShaderCacheEntry *entry) {
    {
      std::lock_guard lock(mutex_);
      if (stopped_)
        return;
      if (!thread_.joinable())
        thread_ = std::thread(&ShaderPreparer::Run, this);
      if (!items_.try_emplace(entry).second)
        return;  // queued, being made or made already
      device_ = device;
      queue_.push_back(entry);
      // Ones made and never drawn (an object kept off screen) go first.
      while (items_.size() > kMaxItems && !made_.empty()) {
        auto it = items_.find(made_.front());
        made_.pop_front();
        if (it != items_.end() && it->second.state == State::Made)
          items_.erase(it);
      }
    }
    work_.notify_one();
  }

  std::unique_ptr<plume::RenderShader> Take(plume::RenderDevice *device, const ShaderCacheEntry &entry) {
    {
      std::unique_lock lock(mutex_);
      auto it = items_.find(&entry);
      if (it != items_.end() && it->second.state == State::Making) {
        done_.wait(lock, [&] {
          it = items_.find(&entry);
          return it == items_.end() || it->second.state != State::Making;
        });
      }
      if (it != items_.end()) {
        std::unique_ptr<plume::RenderShader> shader = std::move(it->second.shader);
        // Queued: dropped here, and the thread skips it.
        items_.erase(it);
        if (shader)
          return shader;
      }
    }
    return CreateCachedShader(device, entry);
  }

  void Stop() {
    {
      std::lock_guard lock(mutex_);
      stopped_ = true;
    }
    work_.notify_one();
    if (thread_.joinable())
      thread_.join();
    std::lock_guard lock(mutex_);
    items_.clear();
    queue_.clear();
    made_.clear();
  }

private:
  enum class State { Queued, Making, Made };
  struct Item {
    State state = State::Queued;
    std::unique_ptr<plume::RenderShader> shader;
  };
  // A level makes a few thousand shaders.
  static constexpr size_t kMaxItems = 8192;

  void Run() {
#if defined(_WIN32)
    SetThreadDescription(GetCurrentThread(), L"renut shader preparer");
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), "renut shaders");
#endif
    std::unique_lock lock(mutex_);
    for (;;) {
      work_.wait(lock, [this] { return stopped_ || !queue_.empty(); });
      if (stopped_)
        return;
      const ShaderCacheEntry *entry = queue_.front();
      queue_.pop_front();
      auto it = items_.find(entry);
      if (it == items_.end() || it->second.state != State::Queued)
        continue;  // taken meanwhile
      it->second.state = State::Making;
      plume::RenderDevice *device = device_;
      lock.unlock();
      std::unique_ptr<plume::RenderShader> shader = CreateCachedShader(device, *entry);
      lock.lock();
      // Nothing erases an item being made (Take waits for it).
      it = items_.find(entry);
      it->second.state = State::Made;
      it->second.shader = std::move(shader);
      made_.push_back(entry);
      done_.notify_all();
    }
  }

  std::mutex mutex_;
  std::condition_variable work_;  // queued, or stopping
  std::condition_variable done_;  // one made
  std::unordered_map<const ShaderCacheEntry *, Item> items_;
  std::deque<const ShaderCacheEntry *> queue_;  // to make, in order
  std::deque<const ShaderCacheEntry *> made_;   // made, oldest first (may have been taken)
  plume::RenderDevice *device_ = nullptr;
  std::thread thread_;
  bool stopped_ = false;
};

} // namespace

void PrepareCachedShader(plume::RenderDevice *device, const ShaderCacheEntry *entry) {
  if (device && entry)
    ShaderPreparer::Get().Prepare(device, entry);
}

std::unique_ptr<plume::RenderShader> TakeCachedShader(plume::RenderDevice *device,
                                                      const ShaderCacheEntry &entry) {
  return ShaderPreparer::Get().Take(device, entry);
}

void StopShaderPreparer() {
  ShaderPreparer::Get().Stop();
}

uint64_t ShaderCacheHostBytes() {
  return g_decompressed_bytes.load(std::memory_order_relaxed);
}

void CaptureMissingShader(const uint8_t *container, bool pixel_shader, uint64_t hash) {
  std::lock_guard lock(g_capture_mutex);
  if (!g_captured.insert(hash).second)
    return;

  const auto *header = reinterpret_cast<const d3d::ShaderContainer *>(container);
  const size_t size = size_t(u32(header->virtualSize)) + u32(header->physicalSize);
  const std::filesystem::path dir(REXCVAR_GET(renut_shader_capture_dir));
  const std::filesystem::path path =
      dir / fmt::format("{}_{:016x}.bin", pixel_shader ? "ps" : "vs", hash);

  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream out(path, std::ios::binary);
  if (out)
    out.write(reinterpret_cast<const char *>(container), std::streamsize(size));
  if (!out) {
    RGPU_WARN("Shader {:016X} is not in the shader cache and could not be captured to {}", hash,
              path.string());
    return;
  }
  RGPU_WARN("Shader {:016X} is not in the shader cache: captured to {}, the next build adds it",
            hash, path.string());
}

} // namespace renut::gpu
