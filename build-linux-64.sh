#!/bin/bash

BUILD_DIR=build

if [ -d "$BUILD_DIR" ]; then
  echo "$BUILD_DIR already exists, remove it first"
  exit 1
fi

cmake --version

mkdir "$BUILD_DIR"
cd "$BUILD_DIR"
wget https://github.com/embree/embree/releases/download/v2.15.0/embree-2.15.0.x86_64.linux.tar.gz -O embree.tgz
tar xf embree.tgz
cmake .. -DCMAKE_BUILD_TYPE=Release -Dembree_DIR="$(pwd)/embree-2.15.0.x86_64.linux"
make -j8 VERBOSE=1
make -j8 VERBOSE=1 testlight
make -j8 VERBOSE=1 testqbsp
cpack

# run tests
./light/testlight || exit 1
./qbsp/testqbsp || exit 1

# check rpath
readelf -d ./light/light
unzip -X ericw-tools-*.zip
readelf -d ./ericw-tools-*/bin/light

# coarse tests on real maps (only checks success/failure exit status of tool)
cd ..
export PATH="$(pwd)/$BUILD_DIR/qbsp:$(pwd)/$BUILD_DIR/light:$PATH"
cd testmaps
./automatated_tests.sh || exit 1

# test id1 maps for leaks
cd quake_map_source
./leaktest.sh || exit 1
