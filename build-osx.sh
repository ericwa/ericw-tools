#!/bin/bash

python3 -m pip install -r docs/requirements.txt --force-reinstall

BUILD_DIR=build-osx
EMBREE_ZIP="https://github.com/RenderKit/embree/releases/download/v4.4.0/embree-4.4.0.x86_64.macosx.zip"
EMBREE_ZIP_NAME=$(basename "$EMBREE_ZIP")
EMBREE_DIR_NAME="embree4"

TBB_TGZ="https://github.com/uxlfoundation/oneTBB/releases/download/v2021.11.0/oneapi-tbb-2021.11.0-mac.tgz"
TBB_TGZ_NAME=$(basename "$TBB_TGZ")
TBB_DIR_NAME="oneapi-tbb-2021.11.0"

if [ -d "$BUILD_DIR" ]; then
  echo "$BUILD_DIR already exists, remove it first"
  exit 1
fi

mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

# extract embree into EMBREE_DIR_NAME
wget -q "$EMBREE_ZIP"
unzip -q "$EMBREE_ZIP_NAME" -d "$EMBREE_DIR_NAME"

# extract TBB into .
wget -q "$TBB_TGZ"
tar xf "$TBB_TGZ_NAME"

EMBREE_CMAKE_DIR="$(pwd)/${EMBREE_DIR_NAME}/lib/cmake/embree-4.4.0"
TBB_CMAKE_DIR="$(pwd)/${TBB_DIR_NAME}/lib/cmake/tbb"

# check USE_ASAN environment variable (see cmake.yml)
if [ "$USE_ASAN" == "YES" ]; then
  cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH="$EMBREE_CMAKE_DIR;$TBB_CMAKE_DIR" -DCMAKE_OSX_ARCHITECTURES=x86_64 -DENABLE_LIGHTPREVIEW=YES -DERICWTOOLS_ASAN=YES
else
  cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$EMBREE_CMAKE_DIR;$TBB_CMAKE_DIR" -DCMAKE_OSX_ARCHITECTURES=x86_64
fi
make -j8 package || exit 1

# print shared libraries used
otool -L ./light/light
otool -L ./qbsp/qbsp
otool -L ./vis/vis
otool -L ./bspinfo/bspinfo
otool -L ./bsputil/bsputil

# run tests
./tests/tests || exit 1
