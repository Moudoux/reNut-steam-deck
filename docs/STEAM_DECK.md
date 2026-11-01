# reNut on the Steam Deck (this fork)

This fork of [masterspike52/reNut](https://github.com/masterspike52/reNut) is
built for one target: the Steam Deck (Zen 2 CPU, RDNA 2 GPU, 15 W shared
power, SteamOS). It carries build tooling, renderer work and Rexglue SDK fixes
that make the game run at a steady 60 FPS there. This page is the record of
all of it: read it before changing the renderer, the build, or rebasing.

## Rebasing on upstream: keep every change listed here

Upstream moves fast. When rebasing or merging upstream's `main`:

- **Every change in [What this fork changes](#what-this-fork-changes) must
  survive**, unless upstream fixed the same problem itself. In that case keep
  upstream's version, drop ours, and note it in this file.
- Upstream code that conflicts with a change here: rework our change onto it,
  don't drop it.
- `deck/xenosrecomp-deck.patch` must still apply to the XenosRecomp commit
  upstream's submodule pins: when upstream moves it, carry the patch over,
  hunk by hunk, unless upstream fixed the same thing.
- The SDK patch (`deck/rexglue-sdk-v0.10.0-deck.patch`) must be carried to any
  new SDK version upstream moves to (`renut_manifest.toml`'s `sdk_version`),
  hunk by hunk, unless the SDK fixed the same thing.
- After a rebase, build with `deck/build.sh` and check on the Deck
  ([Checking a build](#checking-a-build)).

Add `upstream` once: `git remote add upstream https://github.com/masterspike52/reNut.git`.

## Building

`deck/build.sh` does everything, in Docker (`deck/Dockerfile`, Ubuntu 24.04):

- **Why a container:** glibc 2.39 there is below SteamOS's, so the binary
  runs on the Deck. A build on the host (Arch, glibc 2.44) fails on the Deck
  with `GLIBC_2.4x not found`.
- **Rexglue SDK:** clones v0.10.0 (commit `c94f5eb`) into `out/rexglue-sdk`,
  applies `deck/rexglue-sdk-v0.10.0-deck.patch`, and builds and installs it
  (Release, Zen 2 flags).
- **XenosRecomp:** initialises upstream's submodule (recursively) and applies
  `deck/xenosrecomp-deck.patch`.
- **DXC:** downloads Microsoft's Linux release (1.9.2609) over the one the
  submodule's `dxc-bin` pins (8.0.4662), so the shaders come from the compiler
  this fork was measured with.
- **plume:** initialises the submodule (`.gitmodules` was missing upstream).
- **Codegen, configure, build:** runs `rexglue codegen`, configures the
  `linux-amd64-release` preset with `-march=znver2 -ffp-contract=off
  -gline-tables-only`, and builds `out/build/linux-amd64-release/renut-x86_64.AppImage`.

Re-running it is the incremental build. `deck/build.sh --shell` opens a shell
in the container with the same mounts.

- **Image kept current:** the image carries a hash of `deck/Dockerfile`
  (label `renut.dockerfile`) and is rebuilt when the file changes. An image
  built before the Dockerfile gained `libtbb-dev` had been reused, and
  XenosRecomp, reconfigured in it, recompiled the shader cache on one core.
- **XenosRecomp configured without TBB** (its CMake cache says
  `TBB_DIR-NOTFOUND`): `build.sh` deletes its build directory so it is
  configured again in the current image.

`linux-amd64-release`, not `-dist`: `-dist` turns on reNut's self-updater,
which would replace this build with upstream's.

### Game files (not in the repository)

`assets/` is gitignored and must hold:

- `default.xex`: the US (NTSC) disc's, from the game folder reNut's launcher
  extracts (`~/.local/share/renut/game/default.xex`).
- `default.xexp`: Title Update #3. The recompiled code is TU3's; codegen
  applies the patch (it logs `XEX patch applied successfully ... 0.0.3.1`).
  Extract it from the US package (media ID `3E567DFF`, file `TU_16L61VD_0000004000000.00000000000O1`):
  `python3 deck/stfs_extract.py <TU file> default.xexp assets/default.xexp`
  (`--all <TU file> <dir>` extracts the whole update).
- `Bundle`: the game's `Bundle` folder or a symlink to it (the shader dump and
  the launcher music read it). `build.sh` mounts a symlink's target read-only.

## Deploying to the Deck

`deck/deploy.sh [--wait]` copies the AppImage over ssh (`deck@steamdeck`, key
login) once the game is not running, checks its sha256 on both sides, and
only then replaces `renut.AppImage`.

Layout on the Deck (SD card `SM256`):

```
/run/media/deck/SM256/Recompilations/reNut/
├── renut.AppImage          the Steam shortcut's target
└── game/                   extracted game, default.xexp (TU3), update/ (TU3's files)
```

- **`renut.cfg`** (`~/.local/share/renut/`): points `game_data_root` and
  `update_data_root` there.
- **`renut.toml`**: holds the Deck's settings (below).
- **NutStaller:** no longer used; the launcher extracts the game itself.

### Settings used on the Deck (`~/.local/share/renut/renut.toml`)

- `renut_lod_distance = "1x"`, `renut_grass_distance = "1x"`: the console's
  distances. reNut defaults to 2x, which costs the hub a lot of draws.
- MSAA left at the game's. Turning it off measured no GPU saving.
- Profiling (cheap, safe to leave on while testing):
  `renut_gpu_profile = 10.0`, `renut_render_profile = 10.0`,
  `renut_render_profile_stages = false`.

reNut rewrites `renut.toml` on exit: edit it only while the game is closed.

## What this fork changes

### Build and repository

- `deck/`: Dockerfile, `build.sh` (rebuilds the image when the Dockerfile
  changes; reconfigures a XenosRecomp built without TBB), `deploy.sh`, the SDK
  patch, `stfs_extract.py`.
- `.gitmodules`: plume (upstream had the gitlink without the entry, so a clone
  had an empty `thirdparty/plume`); `ignore = dirty` on XenosRecomp, which
  `build.sh` patches.
- `version.txt` (`0.0.0`): CMake reads it and upstream doesn't commit it.

### XenosRecomp (`deck/xenosrecomp-deck.patch`)

Applied by `build.sh` to upstream's submodule, `thirdparty/reblue-XenosRecomp`.
To change it: edit the files there, then
`git -C thirdparty/reblue-XenosRecomp diff -- XenosRecomp > deck/xenosrecomp-deck.patch`.


- **One thread → all cores:** links TBB on Linux (`CMakeLists.txt`).
  libstdc++ runs `std::execution` loops serially without it.
- **Same goal:** the shader loop runs over a vector of pointers to the map's
  entries (`main.cpp`). libstdc++ only parallelises random-access ranges,
  and a `std::map`'s aren't. It also uses `par`, not `par_unseq`, because
  the body takes a mutex.
- **Exit crash:** each worker thread's `DxcCompiler` is deliberately never
  freed. Destroying it at thread exit called into `libdxcompiler` after its
  own thread-locals were gone (SIGSEGV).

Result: the shader cache step went from ~30 min on one core to minutes.

### Rexglue SDK (`deck/rexglue-sdk-v0.10.0-deck.patch`)

- **`/dev/shm` leak:** `memory_posix.cpp` now `shm_unlink`s the guest memory
  right after creating it. Before, every session the game didn't close
  cleanly (Steam kills it) left a ~470 MB `/dev/shm/xenia_memory_*`. After
  about a dozen sessions new ones died of SIGBUS at startup and took
  Steam's UI down with them.
- **Audio worker spin:** `threading_posix.cpp` `WaitMultiple` backs off with
  short sleeps when a mutex is contended (it spun on `yield()`), and counts
  its remaining time in microseconds. Rounded to milliseconds, under one
  became a zero sleep, i.e. a spin. The alertable wait's slice rounds up for
  the same reason. The audio worker was ~21% of the game's CPU samples
  spinning in here.
- **Timer thread spin:** `timer_queue.cpp` uses disruptorplus's
  `blocking_wait_strategy` instead of `spin_wait_strategy`.
- **Library bug:** disruptorplus's `blocking_wait_strategy.hpp` passed
  `wait_for`/`wait_until` the time and predicate in the wrong order. It
  had never been compiled.

### reNut

- **Steam on-screen keyboard** (`src/renut_app.h`): sets
  `SDL_HINT_ENABLE_SCREEN_KEYBOARD=0`. The overlay keeps SDL text input on,
  and on a Deck SDL3 then always asks Steam for its keyboard at launch.
- **Index / stream / fetch cache lookups** (`EntryMemo` in
  `gpu/render/draw.cpp`): first try the entry the same-numbered lookup used
  last frame (draws come in the same order every frame), read from an
  in-order list, before the hash/slot lookup. The slot tables are a random
  cache line each, and two cache misses per lookup were ~13% of the render
  thread. `NextFrame()` resets the count each frame.
- **Shader lookups** (`FindShaderLocked` in `gpu/d3d/hooks/shader.cpp`): the
  same in-order prediction. Any `ForgetShader` bumps a generation, so a
  prediction never outlives its record.
- **Cache sweep** (`AdvanceStreamCacheFrameLocked`): sweeps each map for
  minute-idle entries every 600 frames, the three maps 200 frames apart.
  Before, all three every 60 frames: a frame over budget every second. A
  memo is cleared only when its map actually lost entries.
- **Memexport check** (`DrawNeedsExportFlush`): walks a per-declaration
  `stream_mask` (`VertexDeclarationRecord`, set in `BuildDeclarationLayout`),
  not every element byte-swapped per draw.
- **Stream loops** (`BindStreams`, `ConvertExportedStreamsLocked`): walk
  `HostVertexLayout::used_mask`, not all 16 streams' 32-byte entries
  (8 cache lines a draw).
- **Texture upload thread** (`UploadThread` in `gpu/render/textures.cpp`):
  tiled textures' guest data (untiling and conversion, `ReadLevel`) is read
  on a thread of its own from the draw that uploads them, not at the draw.
  The copies are still recorded at the draw; only their staging (the frame's
  upload ring) has to be filled before the command list is submitted, as the
  linear textures' fills already were. At submission
  (`FlushTextureFillsLocked`) the render thread and the `parallel.h` helpers
  read whatever the thread has not started, then wait for its current
  texture. Nothing is drawn differently: no placeholder, no frame late.
  - Why: perf on the Deck (hub arrival from Mumbo's workshop, 334 uploads in
    one frame) had `ReadLevel` at 54 ms of a 91 ms frame. Creating the
    textures and recording barriers and copies were negligible.
  - One thread, for the 15 W budget. It sleeps when idle (no CPU time
    standing in the hub).
  - Tried first and dropped: copies recorded frames later, drawing the old
    contents or the null texture meanwhile. Textures whose host copy is
    rebuilt (another header over the same memory, say) had no old contents
    and flickered black on the title screen and in the hub.
- **Font cache checked by its unlocks** (`g_unlocked_early` in
  `gpu/render/textures.cpp`): a texture unlocked (`D3D::UnlockResource`)
  before it had a host texture, or a lock-driven one evicted, starts
  lock-driven when its host texture is made. N&B fills its font cache
  (1920x1920, ~7 MB) at a load, before anything binds it, and in the hub no
  glyph was added after: no later unlock made it lock-driven, so it was
  hashed whole at every frame that drew text. Only resources whose header
  says texture are remembered (not vertex buffers); the set is cleared past
  4096 entries.
  - Measured in the hub: linear textures' hashing went from 4.1–4.4 MB a
    frame to 0.12–0.18 MB (the font cache: 13 hashes per 600 frames, the
    30-frame verify, instead of ~350). Text still updates.
- **Profiling:**
  - `renut_gpu_profile_draws`: splits `renut_gpu_profile` by shader pair,
    with draw counts. The query pool is 16384 only in that mode; at 2048
    otherwise, because Vulkan writes every query each frame.
  - `renut_render_profile_stages = false`: logs only busy ms, draws and
    **µs per draw**, the number to compare builds by (it doesn't depend on
    the view).
  - **Spike log:** with `renut_render_profile` on, every frame whose
    render-thread work passes 1/60 s logs its ms, draws, pipelines built,
    textures uploaded, how many of those the upload thread read, and the ms
    spent finishing its reads at submission (at most 30 a minute).
  - **Linear textures:** with `renut_render_profile` on, every 600 frames a
    `Linear textures:` line: how many were checked, the hashes and MB hashed
    per frame, and the 8 that cost most, each with its hashes, changes,
    uploads, unlocks and mode (lock-driven, dynamic).

### Measured on the Deck (hub, standing, 2026-11-01)

| | Start | Now |
|---|---|---|
| Frames / 10 s | 473–585 | 598–600 |
| Render thread, µs per draw (~5,200 draws) | ~3.2 | 2.57–2.60 |
| Render thread spikes standing still | constant | none |
| Audio worker | spinning (~21% of samples) | ~3% of a core |
| `/dev/shm` after a killed session | +470 MB each | freed |

The GPU is then ~89% busy at ~950 MHz, with the package at its 15 W cap.

### Measured on the Deck (texture upload thread, 2026-11-01)

From Mumbo's workshop out to the hub, the same walk before and after:

| Frame | Before | After |
|---|---|---|
| Arrival, 334 textures uploaded | 91.4 ms | 36.5 ms (14.9 ms finishing reads at submission) |
| 23 uploads / 11 uploads | 44.0 / 30.0 ms | 39.6 / 28.2 ms (no wait: 7,400 and 9,500 draws) |
| Streaming in, 6–21 uploads | 18.4–20.8 ms | 16.7–17.2 ms (0–0.9 ms wait) |
| Standing in the hub | 600 frames / 10 s, ~2.6 µs per draw | 600–601 frames / 10 s, 2.60–2.67 µs per draw |

## Profiling on the Deck

- **Logs:** `~/.local/share/renut/logs/renut.log`. Look for `GPU profile:`,
  `Render thread profile: … (x us per draw)` and `Render thread spike:`.
- **CPU samples:**
  - Find the render thread: the `[tNNN]` of `GPU profile:` lines.
  - Record: `perf record -F 499 --call-graph dwarf,16384 -t <tid> -o /tmp/x.data -- sleep 8`.
  - Read: `perf report --children --sort sym -g folded,0,caller,count`.
  - By line: `-F 999` without `-g`, then `--sort srcline`. The build has line
    tables.
  - By time, for one spike among normal frames: `perf script --inline -F
    time,ip,sym --time <start>,<end>` and count samples per 100 ms by the
    functions on their stacks (at 499 Hz a sample is ~2 ms). Busy stretches
    show where a load ends. Run the awk from a script file copied to the
    Deck; nested ssh quoting expands its `$` fields locally. `perf report
    --time <start>,<end>` gives the leaf functions of the same window.
  - Delete the data file afterwards.
- **Per-thread CPU from `/proc/<pid>/task/*/stat`:** parse after the last
  `) `. Thread names contain spaces.
- **GPU clock / busy / power:** `/sys/class/drm/card0/device/{pp_dpm_sclk,gpu_busy_percent}`
  and `hwmon*/power1_average`.
- **Missing shaders:** the game captures them to
  `~/.local/share/renut/shader_capture/`. Copy them into
  `out/build/linux-amd64-release/shader_capture/` and rebuild to add them to
  the cache.

## Checking a build

On the Deck, after deploying:

1. The game starts, renders, and Steam's keyboard doesn't appear.
2. Standing in the hub: `Render thread profile` around 2.6 µs per draw and
   ~600 frames per 10 s, with no `Render thread spike` lines while standing
   still.
3. After quitting: `ls /dev/shm/xenia_memory_*` finds nothing.

## Next work

### Tracked frame-time problems

Found measuring the texture upload thread (2026-11-01). Each: what is known,
how to see it, and what to find out first.

1. **A ~780 ms frame at the end of the workshop's load.** Entering Mumbo's
   workshop from the hub, after the loading screen: one frame of 779.7 ms
   with 9,416 draws, 2 textures uploaded, no pipelines built, then 64 ms
   with 112 uploads (`Render thread spike` lines, 16:06:23). Neither
   uploads nor pipelines, so its cause is unknown. Behind the loading screen,
   so it may hardly show. First: perf over the end of that load (start
   recording before leaving the hub: the last attempt stopped ~3 s early),
   and see whether it happens on every entry.
2. **Arrival frames in the hub.** Out of the workshop into the hub, the first
   frames take 20–40 ms with 6,000–9,500 draws and no texture wait (39.6 ms
   at 7,384 draws, 28.2 ms at 9,551). A guess, not measured: the first
   conversions of the area's vertices and indices, and cold caches. This is
   the hitch still visible on arriving. First: perf bucketed by time over the
   arrival ([Profiling on the Deck](#profiling-on-the-deck), by time).
3. **Linear textures' `ContentHash`: fixed.** It was ~3.6% of the render
   thread walking the hub: nearly all of it the font cache, hashed whole at
   every frame that drew text (4 MB a frame). See "Font cache checked by its
   unlocks" above.

### Other

- The render thread's remaining per-draw cost is spread out: texture
  binding ~15%, `DrawNeedsExportFlush` ~12%, `UploadFloatConstants` ~6%.
- Profile-guided optimisation (PGO) from a Deck play session.
- Send the XenosRecomp and SDK fixes upstream.
