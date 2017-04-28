#!/bin/bash

if [ -d build ]; then
  echo "build already exists, remove it first"
  exit 1
fi

cmake --version

mkdir build
cd build
wget https://github.com/embree/embree/releases/download/v2.15.0/embree-2.15.0.x86_64.linux.tar.gz -O embree.tgz
tar xf embree.tgz
cmake .. -DCMAKE_BUILD_TYPE=Release -Dembree_DIR="$(pwd)/embree-2.15.0.x86_64.linux/lib/cmake/embree-2.15.0"
make -j8 VERBOSE=1
make -j8 VERBOSE=1 testlight
cpack

# run tests
./light/testlight || exit 1

# check rpath
readelf -d ./light/light
unzip -X tyrutils-*.zip
readelf -d ./tyrutils-*/bin/light

