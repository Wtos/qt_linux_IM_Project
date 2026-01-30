#!/bin/bash
set -e

echo "=== Build IM server ==="

rm -rf build
mkdir build
cd build

cmake ..
make -j4

echo "=== Build complete ==="
echo "Run: ./build/im_server 8888"
