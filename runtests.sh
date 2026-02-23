#!/bin/bash
set -e

BUILD_DIR="build/Release"

cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=ON \
    -DCMAKE_INSTALL_PREFIX=install

cmake --build "${BUILD_DIR}" -j$(nproc)

"${BUILD_DIR}/tgfx_tests" "$@"
