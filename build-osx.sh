#!/bin/bash

BUILD_DIR=build-osx
EMBREE_TGZ="https://github.com/embree/embree/releases/download/v2.17.7/embree-2.17.7.x86_64.macosx.tar.gz"
EMBREE_TGZ_NAME=$(basename "$EMBREE_TGZ")
EMBREE_DIR_NAME=$(basename "$EMBREE_TGZ" ".tar.gz")
EMBREE_WITH_VERSION=$(basename "$EMBREE_TGZ" ".x86_64.macosx.tar.gz")

TBB_TGZ="https://github.com/intel/tbb/releases/download/2017_U7/tbb2017_20170604oss_mac.tgz"
TBB_TGZ_NAME=$(basename "$TBB_TGZ")
TBB_DIR_NAME="tbb2017_20170604oss"

if [ -d "$BUILD_DIR" ]; then
  echo "$BUILD_DIR already exists, remove it first"
  exit 1
fi

mkdir "$BUILD_DIR"
cd "$BUILD_DIR"
wget "$EMBREE_TGZ"
wget "$TBB_TGZ"
tar xf "$EMBREE_TGZ_NAME"
tar xf "$TBB_TGZ_NAME"

EMBREE_CMAKE_DIR="$(pwd)/$EMBREE_DIR_NAME"
TBB_CMAKE_DIR="$(pwd)/${TBB_DIR_NAME}/cmake"
cmake .. -DCMAKE_BUILD_TYPE=Release -Dembree_DIR="$EMBREE_CMAKE_DIR" -DTBB_DIR="$TBB_CMAKE_DIR"
make -j8
make -j8 testlight
make -j8 testqbsp
cpack

# run tests
./light/testlight || exit 1
./qbsp/testqbsp || exit 1

# coarse tests on real maps (only checks success/failure exit status of tool)
cd ..
export PATH="$(pwd)/$BUILD_DIR/qbsp:$(pwd)/$BUILD_DIR/light:$PATH"
cd testmaps
./automatated_tests.sh || exit 1

# test id1 maps for leaks
cd quake_map_source
./leaktest.sh || exit 1
