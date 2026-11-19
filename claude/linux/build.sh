#!/usr/bin/env bash
# 编译 Linux 版（Qt6 + libmpv）
# 用法：./build.sh                      用仓库自带的动画（../assets）
#       ./build.sh 开屏.mp4 循环.mp4     换成你自己的动画
set -e
cd "$(dirname "$0")"

INTRO="${1:-../assets/intro.mp4}"
LOOP="${2:-../assets/loop.mp4}"

[[ -f "$INTRO" && -f "$LOOP" ]] || { echo "找不到视频：$INTRO / $LOOP"; exit 1; }

# 检查依赖
missing=()
command -v cmake >/dev/null || missing+=("cmake")
command -v g++ >/dev/null || missing+=("g++")
pkg-config --exists Qt6Core >/dev/null 2>&1 || missing+=("qt6-base-dev")
pkg-config --exists mpv >/dev/null 2>&1 || missing+=("libmpv-dev")
if (( ${#missing[@]} )); then
  echo "缺少依赖：${missing[*]}"
  echo
  echo "Debian / Ubuntu  安装命令："
  echo "  sudo apt install cmake g++ qt6-base-dev libmpv-dev ffmpeg"
  echo "Fedora："
  echo "  sudo dnf install cmake gcc-c++ qt6-qtbase-devel mpv-libs-devel ffmpeg"
  echo "Arch："
  echo "  sudo pacman -S cmake qt6-base mpv ffmpeg"
  exit 1
fi

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

echo "编译完成：$(pwd)/build/ClaudeBootAnimation"
echo "跑一次预览：./build/ClaudeBootAnimation --preview"
