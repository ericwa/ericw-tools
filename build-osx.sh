#!/bin/bash

# for sha256sum, used by the tests
brew install coreutils

BUILD_DIR=build-osx
EMBREE_ZIP="https://github.com/embree/embree/releases/download/v3.13.0/embree-3.13.0.x86_64.macosx.zip"

# embree-3.13.1.x86_64.macosx.zip
EMBREE_ZIP_NAME=$(basename "$EMBREE_ZIP")

# embree-3.13.1.x86_64.macosx
EMBREE_DIR_NAME=$(basename "$EMBREE_ZIP_NAME" ".zip")

TBB_TGZ="https://github.com/oneapi-src/oneTBB/releases/download/v2021.2.0/oneapi-tbb-2021.2.0-mac.tgz"
TBB_TGZ_NAME=$(basename "$TBB_TGZ")
TBB_DIR_NAME="oneapi-tbb-2021.2.0"

if [ -d "$BUILD_DIR" ]; then
  echo "$BUILD_DIR already exists, remove it first"
  exit 1
fi

mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

wget -q "$EMBREE_ZIP"
unzip -q "$EMBREE_ZIP_NAME"

wget -q "$TBB_TGZ"
tar xf "$TBB_TGZ_NAME"

EMBREE_CMAKE_DIR="$(pwd)/$EMBREE_DIR_NAME/lib/cmake/embree-3.13.0"
TBB_CMAKE_DIR="$(pwd)/${TBB_DIR_NAME}/lib/cmake"
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$EMBREE_CMAKE_DIR;$TBB_CMAKE_DIR"
make -j8 || exit 1
make -j8 testlight || exit 1
make -j8 testqbsp || exit 1
cpack || exit 1

# print shared libraries used
otool -L ./light/light
otool -L ./qbsp/qbsp
otool -L ./vis/vis
otool -L ./bspinfo/bspinfo
otool -L ./bsputil/bsputil

# run tests
./light/testlight || exit 1
./qbsp/testqbsp || exit 1

# run regression tests
cd ..
export PATH="$(pwd)/$BUILD_DIR/qbsp:$PATH"
export PATH="$(pwd)/$BUILD_DIR/vis:$PATH"
export PATH="$(pwd)/$BUILD_DIR/light:$PATH"
export PATH="$(pwd)/$BUILD_DIR/bspinfo:$PATH"
export PATH="$(pwd)/$BUILD_DIR/bsputil:$PATH"
cd testmaps
./automatated_tests.sh || exit 1
