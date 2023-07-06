#!/bin/bash

rm -fr time_compilation_build
mkdir time_compilation_build
cd time_compilation_build

cmake .. -DCMAKE_TOOLCHAIN_FILE="d:/vcpkg/scripts/buildsystems/vcpkg.cmake" -DENABLE_LIGHTPREVIEW=YES -DDISABLE_DOCS=YES
time cmake --build .
