#!/bin/bash

python3 -m pip install -r docs/requirements.txt --force-reinstall

BUILD_DIR=build-osx
EMBREE_ZIP="https://github.com/embree/embree/releases/download/v3.13.1/embree-3.13.1.x86_64.macosx.zip"

# embree-3.13.1.x86_64.macosx.zip
EMBREE_ZIP_NAME=$(basename "$EMBREE_ZIP")

# embree-3.13.1.x86_64.macosx
EMBREE_DIR_NAME=$(basename "$EMBREE_ZIP_NAME" ".zip")

TBB_TGZ="https://github.com/oneapi-src/oneTBB/releases/download/v2021.3.0/oneapi-tbb-2021.3.0-mac.tgz"
TBB_TGZ_NAME=$(basename "$TBB_TGZ")
TBB_DIR_NAME="oneapi-tbb-2021.3.0"

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

EMBREE_CMAKE_DIR="$(pwd)/$EMBREE_DIR_NAME/lib/cmake/embree-3.13.1"
TBB_CMAKE_DIR="$(pwd)/${TBB_DIR_NAME}/lib/cmake"

# check USE_ASAN environment variable (see cmake.yml)
if [ "$USE_ASAN" == "YES" ]; then
  cmake .. -DCMAKE_TOOLCHAIN_FILE="$VCPKG_CMAKE_FILE" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH="$EMBREE_CMAKE_DIR;$TBB_CMAKE_DIR" -DENABLE_LIGHTPREVIEW=YES -DERICWTOOLS_ASAN=YES
else
  cmake .. -DCMAKE_TOOLCHAIN_FILE="$VCPKG_CMAKE_FILE" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$EMBREE_CMAKE_DIR;$TBB_CMAKE_DIR"
fi
make -j8 package || exit 1

# print shared libraries used
otool -L ./light/light
otool -L ./qbsp/qbsp
otool -L ./vis/vis
otool -L ./bspinfo/bspinfo
otool -L ./bsputil/bsputil

# run tests
if [ "$USE_ASAN" != "YES" ]; then
  ./tests/tests --no-skip || exit 1 # run hidden tests (releaseonly)
else
  ./tests/tests || exit 1
fi
