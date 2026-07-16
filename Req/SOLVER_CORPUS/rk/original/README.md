# rk/original — Khovratovich's C++ reference, portable fork (C++14)

Upstream: `~/Work/ZK/ZKs/equihash-khovratovich/Source/C++11/` (CC0).
`pow.h` and `pow-test.cc` are **verbatim**; `pow.cc` carries exactly the
three deviations listed in its header comment (modern-reference BLAKE2b
with modern argument order; `std::chrono` timer instead of x86 `rdtsc`
asm; no bundled `blake/`). `vecgen.cc` (new) generates `../vectors/*.json`.

Verified: this build regenerates **all 8 committed vectors byte-identically**
(same nonces, same index arrays) — the fork is behaviorally the upstream
solver.

Build (CMake ≥3.16, no architecture flags, C++14 — works on Linux, macOS
arm64/x86_64; Windows expected via MSVC/MinGW, untested):

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ./build/equihash -n 96 -k 5 -s 4      # upstream's own CLI
    ./build/rk_vecgen 96 5 4              # one JSON vector line

BLAKE2b comes from the repository's vendored portable reference
(`BLAKE/vendor/blake2`, provenance there); `-DBLAKE2_REF_DIR=...`
overrides. Why the fork exists, and why the upstream bundle doesn't build
on modern toolchains: `BLAKE/BLAKE.md` §"Compilation challenges".

Note on the argument-order trap this fork removes: the 2016 bundle
declared `blake2b(out, in, key, outlen, inlen, keylen)`; the modern
reference uses `blake2b(out, outlen, in, inlen, key, keylen)`. In C,
symbols carry **no parameter type or order information** — a mismatched
declaration links silently and corrupts at runtime (C++ name-mangling
would catch it, but these are C-linkage functions). Coding directly to
the modern order, as this fork does, is the only robust fix; shims merely
relocate the hazard.
