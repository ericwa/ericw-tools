#!/bin/bash

python3 -m pip install -r docs/requirements.txt --force-reinstall
export PATH="~/.local/bin/:$PATH"

BUILD_DIR=build-linux

if [ -d "$BUILD_DIR" ]; then
  echo "$BUILD_DIR already exists, remove it first"
  exit 1
fi

cmake --version

mkdir "$BUILD_DIR"
cd "$BUILD_DIR"
wget -q https://github.com/embree/embree/releases/download/v3.13.1/embree-3.13.1.x86_64.linux.tar.gz -O embree.tgz
wget -q https://github.com/oneapi-src/oneTBB/releases/download/v2021.3.0/oneapi-tbb-2021.3.0-lin.tgz -O tbb.tgz

tar xf embree.tgz
tar xf tbb.tgz

EMBREE_CMAKE_DIR="$(pwd)/embree-3.13.1.x86_64.linux/lib/cmake/embree-3.13.1"
TBB_CMAKE_DIR="$(pwd)/oneapi-tbb-2021.3.0/lib/cmake"

# check USE_ASAN environment variable (see cmake.yml)
if [ "$USE_ASAN" == "YES" ]; then
  cmake .. -DCMAKE_TOOLCHAIN_FILE="$VCPKG_CMAKE_FILE" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH="$EMBREE_CMAKE_DIR;$TBB_CMAKE_DIR" -DENABLE_LIGHTPREVIEW=YES -DERICWTOOLS_ASAN=YES
else
  cmake .. -DCMAKE_TOOLCHAIN_FILE="$VCPKG_CMAKE_FILE" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$EMBREE_CMAKE_DIR;$TBB_CMAKE_DIR"
fi

# not yet free of memory leaks, so don't abort on leak detection
export ASAN_OPTIONS=detect_leaks=false

make -j8 VERBOSE=1 package || exit 1

# run tests
if [ "$USE_ASAN" != "YES" ]; then
  ./tests/tests --no-skip || exit 1 # run hidden tests (releaseonly)
else
  ./tests/tests || exit 1
fi

# check rpath
readelf -d ./light/light
