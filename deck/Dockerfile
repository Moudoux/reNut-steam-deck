# Steam Deck build environment for reNut (deck/build.sh builds and uses it).
#
# Ubuntu 24.04: glibc 2.39, below SteamOS's (2.41+), so the binary runs on the
# Deck; a build on a rolling distro (Arch: glibc 2.44) does not. clang 20 as
# the rexglue SDK's own CI uses.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl wget gnupg git git-lfs lsb-release software-properties-common \
      cmake ninja-build build-essential python3 python3-venv ffmpeg unzip zip autoconf pkg-config file \
      libgtk-3-dev libx11-xcb-dev libxss-dev libvulkan-dev glslang-tools spirv-tools \
      libwayland-dev libwayland-bin wayland-protocols libxkbcommon-dev libdecor-0-dev \
      libasound2-dev libpulse-dev libpipewire-0.3-dev libcurl4-openssl-dev libssl-dev zlib1g-dev \
      # tools/dump_shaders.py; XenosRecomp's parallel loop (libstdc++ runs
      # std::execution serially without TBB); gdb for crash backtraces.
      python3-xxhash python3-cryptography libtbb-dev gdb \
 && wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh && chmod +x /tmp/llvm.sh && /tmp/llvm.sh 20 \
 && apt-get install -y clang-20 lld-20 \
 && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-20 200 \
 && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-20 200 \
 && git config --global --add safe.directory '*' && rm -rf /var/lib/apt/lists/*
