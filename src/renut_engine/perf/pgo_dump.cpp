/**
 * @file    perf/pgo_dump.cpp
 * @brief   Writes the PGO profile while the game runs (deck/build.sh --pgo-generate).
 */
#include <chrono>
#include <thread>

#if defined(__linux__)
#include <pthread.h>
#endif

// The profiling runtime's, linked only into a -fprofile-generate build: null
// in any other.
extern "C" int __llvm_profile_write_file(void) __attribute__((weak));
extern "C" void __llvm_profile_reset_counters(void) __attribute__((weak));

namespace {

// The runtime writes the profile at exit, but Steam ends the game by killing
// it, which writes nothing. So every 30 s: the counts since the last write
// are merged into the profile (-fprofile-generate=<dir> merges into
// <dir>/default_<n>.profraw) and counted again from zero.
struct PgoDump {
  PgoDump() {
    if (!__llvm_profile_write_file || !__llvm_profile_reset_counters)
      return;
    std::thread([] {
#if defined(__linux__)
      pthread_setname_np(pthread_self(), "renut pgo");
#endif
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        if (__llvm_profile_write_file() == 0)
          __llvm_profile_reset_counters();
      }
    }).detach();
  }
} g_pgo_dump;

} // namespace
