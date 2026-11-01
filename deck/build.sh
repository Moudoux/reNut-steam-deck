#!/usr/bin/env bash
# Builds reNut for the Steam Deck in an Ubuntu 24.04 container (deck/Dockerfile).
# See docs/STEAM_DECK.md. Each step is skipped when already done, so this is
# also the incremental rebuild.
#
#   deck/build.sh            build (out/build/linux-amd64-release/renut-x86_64.AppImage)
#   deck/build.sh --shell    a shell in the container, mounts as for the build
#
# Needs: docker; assets/default.xex and assets/default.xexp (TU3, see the docs),
# assets/Bundle (the game's Bundle folder, or a symlink to it).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/out/rexglue-sdk"
SDK_COMMIT=c94f5eb   # "Release v0.10.0" (renut_manifest.toml's sdk_version)
IMAGE=renut-deck-build
BUILD="$ROOT/out/build/linux-amd64-release"
DXC_URL=https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/linux_dxc_2026_09_28.x86_x64.tar.gz
DXC_DIR="$ROOT/thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin"
# Zen 2 (the Deck's CPU); no FMA contraction, so floating point (the physics)
# rounds as without -march. -gline-tables-only: perf can name source lines
# (no code change, a few MB).
FLAGS="-march=znver2 -ffp-contract=off -gline-tables-only"

# assets/Bundle may be a symlink to the game's extracted Bundle elsewhere: the
# container sees that folder at the same path, read-only.
GAME_MOUNT=()
if [ -L "$ROOT/assets/Bundle" ]; then
  target="$(readlink -f "$ROOT/assets/Bundle")"
  GAME_MOUNT=(-v "$target:$target:ro")
fi

run() {
  docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$SDK:/sdk" -v "$ROOT:$ROOT" "${GAME_MOUNT[@]}" -w "$ROOT" "$IMAGE" "$@"
}

# Rebuilt whenever the Dockerfile changes (the image carries its hash): an
# image older than its Dockerfile's libtbb-dev left XenosRecomp on one thread.
DOCKERFILE_HASH="$(sha256sum "$ROOT/deck/Dockerfile" | cut -c1-16)"
if [ "$(docker image inspect -f '{{index .Config.Labels "renut.dockerfile"}}' "$IMAGE" 2>/dev/null)" != "$DOCKERFILE_HASH" ]; then
  echo "== build image $IMAGE"
  docker build --label "renut.dockerfile=$DOCKERFILE_HASH" -t "$IMAGE" "$ROOT/deck"
fi

if [ "${1:-}" = "--shell" ]; then
  docker run --rm -it -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$SDK:/sdk" -v "$ROOT:$ROOT" "${GAME_MOUNT[@]}" -w "$ROOT" "$IMAGE" bash
  exit
fi

# The SDK, from source with deck/rexglue-sdk-v0.10.0-deck.patch (threading
# spins, /dev/shm leak). Kept in out/ (gitignored) and installed into
# out/rexglue-sdk/out/install/linux-amd64.
if [ ! -d "$SDK/.git" ]; then
  echo "== clone rexglue-sdk $SDK_COMMIT"
  git clone --recursive https://github.com/rexglue/rexglue-sdk "$SDK"
  git -C "$SDK" checkout "$SDK_COMMIT"
  git -C "$SDK" submodule update --init --recursive
  git -C "$SDK" apply "$ROOT/deck/rexglue-sdk-v0.10.0-deck.patch"
fi
if [ ! -f "$SDK/out/install/linux-amd64/lib/cmake/rexglue/rexglueConfig.cmake" ] ||
   [ -n "$(find "$SDK/src" "$SDK/include" -newer "$SDK/out/install/linux-amd64/lib/librexruntime.so" -print -quit 2>/dev/null)" ]; then
  echo "== build rexglue-sdk (Release)"
  docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$SDK:/sdk" -w /sdk "$IMAGE" bash -c "
    [ -f out/build/linux-amd64/CMakeCache.txt ] ||
      cmake --preset linux-amd64 -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
            '-DCMAKE_C_FLAGS=-march=znver2 -ffp-contract=off' '-DCMAKE_CXX_FLAGS=-march=znver2 -ffp-contract=off'
    cmake --build out/build/linux-amd64 --config Release --target install --parallel"
fi

# XenosRecomp is upstream's submodule, with two things on top of it, done again
# when its commit, the patch or the DXC release changes: deck/xenosrecomp-deck.patch,
# and Microsoft's DXC over the older one its dxc-bin submodule pins.
XR="$ROOT/thirdparty/reblue-XenosRecomp"
XR_PATCH="$ROOT/deck/xenosrecomp-deck.patch"
XR_STAMP="$ROOT/out/xenosrecomp.stamp"
xr_state="$(git -C "$ROOT" rev-parse HEAD:thirdparty/reblue-XenosRecomp) $(sha256sum "$XR_PATCH" | cut -c1-64) $DXC_URL"
if [ "$(cat "$XR_STAMP" 2>/dev/null)" != "$xr_state" ] ||
   [ ! -e "$XR/.git" ] || ! git -C "$XR" apply --reverse --check "$XR_PATCH" 2>/dev/null; then
  echo "== XenosRecomp: submodule, patch, DXC"
  if [ -e "$XR/.git" ]; then
    git -C "$XR" checkout -- XenosRecomp
    if [ -e "$DXC_DIR/.git" ]; then git -C "$DXC_DIR" checkout -- .; fi
  fi
  git -C "$ROOT" submodule update --init --recursive thirdparty/reblue-XenosRecomp
  git -C "$XR" apply "$XR_PATCH"
  tmp="$(mktemp -d)"
  curl -sL -o "$tmp/dxc.tgz" "$DXC_URL"
  tar -xzf "$tmp/dxc.tgz" -C "$tmp"
  cp "$tmp"/lib/*.so "$DXC_DIR/lib/x64/"
  cp "$tmp/bin/dxc" "$DXC_DIR/bin/x64/dxc-linux"
  rm -rf "$tmp"
  mkdir -p "$ROOT/out"
  echo "$xr_state" > "$XR_STAMP"
fi

git -C "$ROOT" submodule update --init thirdparty/plume

for f in assets/default.xex assets/default.xexp; do
  [ -f "$ROOT/$f" ] || { echo "missing $f (docs/STEAM_DECK.md: game files)" >&2; exit 1; }
done
[ -f "$ROOT/version.txt" ] || echo 0.0.0 > "$ROOT/version.txt"

echo "== codegen"
run /sdk/out/install/linux-amd64/bin/rexglue codegen renut_manifest.toml

if [ ! -f "$BUILD/CMakeCache.txt" ]; then
  echo "== configure"
  run cmake --preset linux-amd64-release -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
      -Drexglue_DIR=/sdk/out/install/linux-amd64/lib/cmake/rexglue \
      "-DCMAKE_C_FLAGS=$FLAGS" "-DCMAKE_CXX_FLAGS=$FLAGS"
fi

# XenosRecomp configured without TBB (an image without it) compiles the
# shader cache on one thread: start it again in this image.
XENOS="$BUILD/xenos_recomp"
if [ -f "$XENOS/build/CMakeCache.txt" ] && grep -q '^TBB_DIR:PATH=TBB_DIR-NOTFOUND' "$XENOS/build/CMakeCache.txt"; then
  echo "== XenosRecomp was configured without TBB: reconfiguring"
  rm -rf "$XENOS/build" "$XENOS/src/renut_xenos_recomp-stamp/renut_xenos_recomp-configure"
  mkdir -p "$XENOS/build"
fi

echo "== build"
run cmake --build "$BUILD" --parallel "$(nproc)"
ls -la "$BUILD/renut-x86_64.AppImage"
