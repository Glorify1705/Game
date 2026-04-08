#!/usr/bin/env bash
# game-test: Build and run the GoogleTest suite.
#
# Forces CMAKE_BUILD_TYPE=Debug so sanitizers (ASan/UBSan) are enabled
# for the test binary, per CMakeLists.txt. The `Tests` target both
# compiles and runs the tests.
#
# Arguments: none.
set -euo pipefail
cmake -DCMAKE_BUILD_TYPE=Debug -G Ninja -S . -B build
cmake --build build --target Tests
