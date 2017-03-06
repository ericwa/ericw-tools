#!/bin/bash

BUILD_DIR=build-osx
EMBREE_TGZ="https://github.com/embree/embree/releases/download/v2.14.0/embree-2.14.0.x86_64.macosx.tar.gz"
EMBREE_TGZ_NAME=$(basename "$EMBREE_TGZ")
EMBREE_DIR_NAME=$(basename "$EMBREE_TGZ" ".tar.gz")
EMBREE_WITH_VERSION=$(basename "$EMBREE_TGZ" ".x86_64.macosx.tar.gz")

if [ -d "$BUILD_DIR" ]; then
  echo "$BUILD_DIR already exists, remove it first"
  exit 1
fi

mkdir "$BUILD_DIR"
cd "$BUILD_DIR"
wget "$EMBREE_TGZ"
tar xf "$EMBREE_TGZ_NAME"
EMBREE_CMAKE_DIR="$(pwd)/$EMBREE_DIR_NAME/lib/cmake/$EMBREE_WITH_VERSION"
cmake .. -DCMAKE_BUILD_TYPE=Release -Dembree_DIR="$EMBREE_CMAKE_DIR"
make -j8
cpack

