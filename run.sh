#!/usr/bin/env bash
set -e
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
echo "=== Running Tests ==="
./test_bin
echo "=== Running Emulator ==="
./emulator