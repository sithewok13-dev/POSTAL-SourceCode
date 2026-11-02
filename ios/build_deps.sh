#!/bin/bash
# Builds SDL2 as a static iOS (arm64) library into $1.
set -ex
DEPS="$1"
: "${IOS_MIN:=15.0}" "${SDL_TAG:=release-2.30.8}"
WORK="${RUNNER_TEMP:-/tmp}/ios-deps-src"
rm -rf "$WORK"; mkdir -p "$WORK" "$DEPS"
git clone --depth 1 -b "$SDL_TAG" https://github.com/libsdl-org/SDL.git "$WORK/SDL"
cmake -S "$WORK/SDL" -B "$WORK/SDL/build" -G Ninja -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_MIN" -DCMAKE_BUILD_TYPE=Release \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DSDL_HIDAPI=OFF \
  -DCMAKE_INSTALL_PREFIX="$DEPS"
cmake --build "$WORK/SDL/build"
cmake --install "$WORK/SDL/build"
test -f "$DEPS/lib/libSDL2.a"
