#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CXX="${CXX:-g++}"
command -v "$CXX" >/dev/null 2>&1 || {
  echo "error: C++ compiler not found: $CXX" >&2
  exit 1
}
command -v python3 >/dev/null 2>&1 || {
  echo "error: python3 is required" >&2
  exit 1
}

mkdir -p build/check

compiler_version="$($CXX --version)"
echo "[check] compiler: ${compiler_version%%$'\n'*}"
echo "[check] direct C++20 warning-clean build"
"$CXX" -std=c++20 -Wall -Wextra -Wpedantic -Werror \
  -Icore/include \
  core/src/mesh.cpp tests/test_mesh.cpp \
  -o build/check/octopoly_core_tests
"$CXX" -std=c++20 -Wall -Wextra -Wpedantic -Werror \
  -Icore/include \
  core/src/mesh.cpp core/src/project_codec.cpp tests/test_project_codec.cpp \
  -o build/check/project_codec_tests
"$CXX" -std=c++20 -Wall -Wextra -Wpedantic -Werror \
  -Icore/include \
  core/src/mesh.cpp core/src/project_codec.cpp tests/test_mesh_allocation_faults.cpp \
  -o build/check/mesh_allocation_fault_tests

echo "[check] mesh tests"
./build/check/octopoly_core_tests

echo "[check] project codec tests"
./build/check/project_codec_tests

echo "[check] mesh allocation-fault tests"
./build/check/mesh_allocation_fault_tests

echo "[check] shell syntax"
bash -n scripts/check.sh scripts/mac/remote-build.sh scripts/mac/install-device.sh

echo "[check] deterministic Xcode/bridge/UI static validation"
python3 scripts/validate_project.py

echo "[check] PASS"
