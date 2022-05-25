#!/bin/sh
set -ev

rm -Rf build/clang13
mkdir -p build/clang13
cd build/clang13

export CC=clang-13
export CXX=clang++-13

cmake -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles" ../..

cmake --build .

ctest --output-on-failure

