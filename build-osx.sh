#!/bin/bash

BUILD_DIR=build-osx

if [ -d "$BUILD_DIR" ]; then
  echo "$BUILD_DIR already exists, remove it first"
  exit 1
fi

brew install embree tbb

mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix embree);$(brew --prefix tbb)"
make -j8 || exit 1
make -j8 testlight || exit 1
make -j8 testqbsp || exit 1
cpack || exit 1

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
