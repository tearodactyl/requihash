# uniblake — clone, build, and test on a fresh machine

This is the "hand it to someone with a bare VPS or a fresh Windows box"
guide. It covers cloning the repo and running the full validation on:
**Linux VPS (x86_64)**, **Windows 10 native**, **Windows 11 native**,
and **WSL2**. For the deeper per-toolchain matrix (MinGW, cross-compile,
endianness, MSVC specifics) see [BUILD.md](BUILD.md).

`uniblake` is self-contained inside `BLAKE/uniblake/` — it depends only
on the repo-relative vendored BLAKE2 reference (`BLAKE/vendor/blake2/`)
and, for the NEON kernel, `BLAKE/uniblake/vendor/neon/`. No system
BLAKE library, no network fetch at build time.

## Evidence grades (be honest about what's proven)

- **arm64 macOS** — RUN & GREEN (dev machine, 6/6 ctests).
- **x86_64 Linux, Windows 10/11, WSL2** — the build is portable and the
  code paths are written + guard-balanced, but **not run by us on that
  hardware**. Follow the steps below; the first run on each confirms it
  and you can upgrade the grade in STATUS.md.

## 0. Prerequisites

| Platform | Install |
|---|---|
| Linux VPS (Debian/Ubuntu) | `sudo apt-get update && sudo apt-get install -y git cmake gcc make` |
| Linux VPS (RHEL/Alma/Rocky) | `sudo dnf install -y git cmake gcc make` |
| Windows 10/11 native (MSVC) | Git for Windows + Visual Studio 2022 (or Build Tools) with "Desktop development with C++"; CMake is bundled with VS |
| Windows 10/11 native (MinGW) | Git + [MSYS2](https://www.msys2.org/), then `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake` |
| WSL2 | In the WSL2 distro, same as Linux VPS |

CMake ≥ 3.16 required (any version from 2020 on). Compiler: any C99
compiler (GCC 4.5+, Clang 3.x+, MSVC 2015+).

## 1. Clone

The uniblake tree lives inside the Requihash repo. Clone it, then work
from the uniblake subdirectory:

```sh
git clone <REQUIHASH_REPO_URL> requihash
cd requihash/BLAKE/uniblake
```

(Replace `<REQUIHASH_REPO_URL>` with the actual origin. If you only have
access to a tarball, unpack it and `cd` into `BLAKE/uniblake` — the
build needs the sibling `../vendor/blake2/` present, which the repo
includes.)

## 2a. Linux VPS (x86_64) and WSL2

Identical steps (WSL2 is real x86_64 Linux):

```sh
sh scripts/build_and_test.sh
```

That configures, builds, runs all 6 ctests, and prints the CPU probe +
`abc` KAT. Expected on a modern x86 CPU: `probe: neon=0 sse41=1 avx2=1`;
all tests pass; auto-kernel `ref`. To pick a compiler: `CC=clang sh
scripts/build_and_test.sh`.

Manual equivalent (if you'd rather not use the script):

```sh
cmake -S . -B build -DUB_ENABLE_BROKEN_KERNEL=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## 2b. Windows 10 and Windows 11 native (identical steps)

Windows 10 and 11 build identically — same toolchains, same commands;
nothing in uniblake depends on a Win11-only API. Two routes:

**MSVC (recommended).** Open a *Developer PowerShell for VS 2022* (Start
menu → search "Developer PowerShell"), then:

```powershell
cd requihash\BLAKE\uniblake
powershell -ExecutionPolicy Bypass -File scripts\build_and_test.ps1
```

Or manually:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DUB_ENABLE_BROKEN_KERNEL=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

**MinGW / MSYS2.** From a UCRT64 shell:

```sh
cd /c/.../requihash/BLAKE/uniblake
sh scripts/build_and_test.sh        # the POSIX script works under MSYS2
```

Expected on both Win10 and Win11, modern CPU: `probe: neon=0 sse41=1
avx2=1`; 6/6 tests pass.

## 3. What "pass" looks like

```
100% tests passed, 0 tests failed out of 6
...
[§1d type 2] published 'abc' KAT
  ok   BLAKE2b-512("abc") matches RFC 7693
    probe: neon=<..> sse41=<..> avx2=<..>
    auto-selected kernel: ref
```

The `abc` KAT line is **byte-identical on every platform** (BLAKE2b is
endianness-defined). If it differs across machines, that is a real bug —
report it. The `probe:` line is expected to differ (it reflects the
local CPU); that is the runtime detection working, not a fault.

## 4. Individual binaries (after building)

| Binary | What |
|---|---|
| `ub_test` | full validation + probe/dispatch/gate (human-readable) |
| `ub_stress` | every kernel vs. oracle across a large battery |
| `ub_snapshot_test` | snapshot export/import validation |
| `ub_gate_test` | gate-rejection proof (needs `-DUB_ENABLE_BROKEN_KERNEL=ON`) |
| `ub_bench` | dispatch-cost microbenchmark (U1) |
| `ub_kbench` | kernel head-to-head speed (leaf + bulk) |

(On MSVC these are under `build\Release\`; elsewhere under `build/`.)

## 5. Reporting a result back

If you run this on a new platform, capture:
- `uname -sm` (or Windows CPU/OS) and compiler version,
- the ctest summary line,
- the `probe:` line and the `abc` KAT line,
- the `ub_kbench` output.

That's enough to upgrade the platform's evidence grade in `STATUS.md`
from STRUCTURED to RUN & GREEN.
