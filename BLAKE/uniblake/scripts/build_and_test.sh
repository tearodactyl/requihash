#!/bin/sh
# uniblake — POSIX build + test (Linux VPS, macOS, WSL2, MinGW/MSYS2).
#
# Self-contained: run from anywhere inside a clone. Configures with
# CMake, builds, runs the full ctest suite, and prints the CPU probe
# result and a fixed KAT so cross-machine consistency can be diffed.
#
# Usage:  sh scripts/build_and_test.sh [build-dir]
# Env:    CC=<compiler>  to pick a toolchain (e.g. CC=gcc, CC=clang).
set -eu

# Resolve the uniblake dir (parent of this script's dir), toolchain-agnostic.
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/.." && pwd)
build=${1:-"$root/build"}

echo "== uniblake build+test =="
echo "root:  $root"
echo "build: $build"
echo "cc:    ${CC:-<cmake default>}"
uname -sm 2>/dev/null || true
echo

# Configure (broken kernel ON so the gate-rejection test runs).
cmake -S "$root" -B "$build" -DUB_ENABLE_BROKEN_KERNEL=ON
cmake --build "$build"

echo
echo "== ctest =="
ctest --test-dir "$build" --output-on-failure

echo
echo "== probe + KAT (for cross-machine consistency diff) =="
# ub_test prints the probe line and the abc KAT result.
"$build/ub_test" | grep -E "probe:|abc|auto-selected" || "$build/ub_test"

echo
echo "== kernel benchmark (informational) =="
"$build/ub_kbench" || true

echo
echo "OK"
