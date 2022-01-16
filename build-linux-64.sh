#!/bin/bash

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
  cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH="$EMBREE_CMAKE_DIR;$TBB_CMAKE_DIR" -DERICWTOOLS_ASAN=YES
else
  cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$EMBREE_CMAKE_DIR;$TBB_CMAKE_DIR"
fi

# not yet free of memory leaks, so don't abort on leak detection
export ASAN_OPTIONS=exitcode=0

make -j8 VERBOSE=1 || exit 1
make -j8 VERBOSE=1 testlight || exit 1
make -j8 VERBOSE=1 testqbsp || exit 1
cpack || exit 1

# run tests
./light/testlight || exit 1
./qbsp/testqbsp || exit 1

# check rpath
readelf -d ./light/light
unzip -X ericw-tools-*.zip
readelf -d ./ericw-tools-*/bin/light

# run regression tests
cd ..
export PATH="$(pwd)/$BUILD_DIR/qbsp:$PATH"
export PATH="$(pwd)/$BUILD_DIR/vis:$PATH"
export PATH="$(pwd)/$BUILD_DIR/light:$PATH"
export PATH="$(pwd)/$BUILD_DIR/bspinfo:$PATH"
export PATH="$(pwd)/$BUILD_DIR/bsputil:$PATH"
cd testmaps
./automatated_tests.sh || exit 1
