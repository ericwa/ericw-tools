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

if [ "$USE_SYSTEM_TBB_AND_EMBREE" == "1" ]; then
  if [ "$USE_ASAN" == "YES" ]; then
    cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DERICWTOOLS_ASAN=YES -DSKIP_EMBREE_INSTALL=YES -DSKIP_TBB_INSTALL=YES
  else
    cmake .. -DCMAKE_BUILD_TYPE=Release -DSKIP_EMBREE_INSTALL=YES -DSKIP_TBB_INSTALL=YES
  fi
else
  wget -q https://github.com/RenderKit/embree/releases/download/v4.4.0/embree-4.4.0.x86_64.linux.tar.gz -O embree.tgz
  wget -q https://github.com/uxlfoundation/oneTBB/releases/download/v2021.11.0/oneapi-tbb-2021.11.0-lin.tgz -O tbb.tgz

  mkdir embree4
  tar xf embree.tgz --directory embree4
  tar xf tbb.tgz

  EMBREE_CMAKE_DIR="$(pwd)/embree4/lib/cmake/embree-4.4.0"
  TBB_CMAKE_DIR="$(pwd)/oneapi-tbb-2021.11.0/lib/cmake/tbb"

  # check USE_ASAN environment variable (see cmake.yml)
  if [ "$USE_ASAN" == "YES" ]; then
    cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH="$EMBREE_CMAKE_DIR;$TBB_CMAKE_DIR" -DENABLE_LIGHTPREVIEW=YES -DERICWTOOLS_ASAN=YES
  else
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$EMBREE_CMAKE_DIR;$TBB_CMAKE_DIR"
  fi
fi

# not yet free of memory leaks, so don't abort on leak detection
export ASAN_OPTIONS=detect_leaks=false

make -j8 VERBOSE=1 package || exit 1

# run tests
./tests/tests || exit 1

# check rpath
readelf -d ./light/light
