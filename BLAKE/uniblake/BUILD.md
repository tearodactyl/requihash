# uniblake — build & validation directions per platform

The PoC is one CMake project (`CMakeLists.txt`), no absolute paths, the
vendored reference referenced repo-relative. Per-target detail is
compile-guarded in C, not in the build. This file gives exact
invocation + validation for each target and method.

**What "validated" means here (evidence grades):**

- **arm64 macOS — RUN & GREEN** (this machine, M4, Apple clang 21,
  CMake 4.2). All 4 ctests pass; the CPU probe reports `neon=1` via a
  real `sysctlbyname` call. This is the reference result.
- **x86_64 Linux / Windows — STRUCTURED, NOT YET RUN.** The code paths
  (`cpuid` detection, MSVC/GCC intrinsics) are written and
  preprocessor-guard-balanced, but not compiled/run on real x86 or
  Windows in this session (no such hardware; consistent with the
  program's A7 "gated on real x86" posture). Directions below are
  written to be followed verbatim; the first person with the hardware
  confirms them and upgrades this grade.

## Common: what a successful run prints

```
=== uniblake PoC validation ===
...
[R3 R7] probe / dispatch / forced-impl / gate
    probe: neon=<0|1> sse41=<0|1> avx2=<0|1>
...
=== ALL GREEN (0 failures) ===
```

`ctest` runs four cases: `ub_validation`, `ub_force_ref`,
`ub_force_unrolled`, and (with the broken kernel enabled)
`ub_gate_rejection`.

---

## 1. arm64 macOS (reference — proven)

```sh
cd BLAKE/uniblake
cmake -S . -B build -DUB_ENABLE_BROKEN_KERNEL=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: `probe: neon=1 sse41=0 avx2=0`, all 4 tests pass.

---

## 2. x86_64 Ubuntu 24.04

### 2a. Native (on real or virtualized x86 Ubuntu 24)

```sh
sudo apt-get update && sudo apt-get install -y cmake gcc make
cd BLAKE/uniblake
cmake -S . -B build -DUB_ENABLE_BROKEN_KERNEL=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected on a modern x86 CPU: `probe: neon=0 sse41=1 avx2=1`. The
active kernel is still `ref` (no SIMD kernels in the PoC), but the
probe *result* now exercises the `cpuid` path — this is the point of
running on x86. All 4 tests pass (same scalar kernels, same oracle).

**Validation note**: what x86 adds over the arm64 run is confirming (a)
the `cpuid`-based `ub_detect_cpu()` compiles and returns sane flags,
and (b) the little-endian path and `-O2` codegen still match the
oracle byte-for-byte on a different ISA. It does NOT yet validate any
SIMD kernel (there are none until U2).

### 2b. Docker on this arm64 Mac (emulated amd64)

Proves the x86 build without x86 hardware; slower under emulation.

```sh
cd BLAKE/uniblake
docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src ubuntu:24.04 bash -c '
  apt-get update && apt-get install -y cmake gcc make &&
  cmake -S . -B build-x86 -DUB_ENABLE_BROKEN_KERNEL=ON &&
  cmake --build build-x86 &&
  ctest --test-dir build-x86 --output-on-failure'
```

(Requires the Docker daemon running. `build-x86/` is a separate build
dir so it doesn't collide with a host arm64 `build/`.)

### 2c. Cross-compile from Ubuntu (x86 host → any, or arm host → x86)

For completeness; native or Docker is simpler. Using a cross GCC:

```sh
sudo apt-get install -y gcc-x86-64-linux-gnu cmake
cmake -S . -B build-cross \
  -DCMAKE_C_COMPILER=x86_64-linux-gnu-gcc \
  -DUB_ENABLE_BROKEN_KERNEL=ON
cmake --build build-cross
# Cannot run x86 binaries on a non-x86 host without qemu-user:
#   sudo apt-get install -y qemu-user-static
#   qemu-x86_64-static build-cross/ub_test
```

---

## 3. Windows 11

Three routes, preferred order: **WSL2 (simplest), native MSVC, native
MinGW**.

### 3a. WSL2 (recommended)

Inside a WSL2 Ubuntu 24.04 distro this is exactly case 2a:

```sh
sudo apt-get update && sudo apt-get install -y cmake gcc make
cd BLAKE/uniblake
cmake -S . -B build -DUB_ENABLE_BROKEN_KERNEL=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

WSL2 is real x86_64 Linux, so the `cpuid` path and the Linux
`getauxval` guards are exercised. This is the lowest-friction Windows
validation.

### 3b. Native MSVC (Visual Studio 2022 / Build Tools)

From a *Developer Command Prompt* (so `cl.exe`/CMake are on PATH):

```bat
cd BLAKE\uniblake
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DUB_ENABLE_BROKEN_KERNEL=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

MSVC specifics already handled in the source:
- `ub_probe.c` uses `__cpuidex` from `<intrin.h>` under `_MSC_VER`.
- `_M_X64` / `_M_ARM64` guards select the right arch path.
- `CMakeLists.txt` uses `/W4` (not `-Wall -Wextra`) under MSVC.
- C99: the code is C99-clean; MSVC's C mode compiles it. If a very old
  toolchain balks at C99, the code is also valid C11.

Expected: `probe: neon=0 sse41=1 avx2=1` on a modern CPU; 4 tests pass.

### 3c. Native MinGW-w64

```bat
cd BLAKE\uniblake
cmake -S . -B build -G "MinGW Makefiles" -DUB_ENABLE_BROKEN_KERNEL=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Uses the GCC `cpuid.h` path (same as Linux). Good cross-check that the
MSVC and GCC probe paths agree.

---

## Endianness

`CMakeLists.txt` runs CMake's `TestBigEndian` and defines
`NATIVE_LITTLE_ENDIAN` when appropriate. All current targets (arm64,
x86_64) are little-endian. The reference's load/store are correct
either way; the define only selects a faster path. A big-endian target
(none planned) would still validate against the oracle — that is what
the gate is for.

## Cross-target consistency check (the real cross-platform claim)

The strong cross-platform statement is not "it builds everywhere" but
"it produces identical bytes everywhere," which the oracle gate
enforces locally on each platform. To assert cross-*machine*
consistency, compare the `abc` KAT line and any fixed-input digest
across platforms — they must be byte-identical (BLAKE2b is
endianness-defined, so they will be). A future step (U-phase) can emit
a small digest manifest per platform and diff them in CI.
