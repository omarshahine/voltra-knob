#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host
SRC=firmware/VoltraKnob/src
CXX="clang++ -std=c++17 -Wall -Wextra -I$SRC -Itest"
$CXX test/test_protocol.cpp $SRC/protocol/voltra_protocol.cpp -o build/host/test_protocol
build/host/test_protocol
if [ -f test/test_app_logic.cpp ]; then
  $CXX test/test_app_logic.cpp $SRC/app/app_logic.cpp -o build/host/test_app_logic
  build/host/test_app_logic
fi
