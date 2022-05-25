#!/bin/sh
set -ev

rm -Rf build/gcc10
mkdir -p build/gcc10
cd build/gcc10

export CC=gcc-10
export CXX=g++-10

cmake -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles" ../..

cmake --build .

ctest --output-on-failure

