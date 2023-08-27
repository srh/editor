#!/bin/bash

# g++ -std=c++20 -Wall -Wextra -g -pthread *.cpp -o qwi

mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-Werror -Wall -Wextra" ..
make -j8
