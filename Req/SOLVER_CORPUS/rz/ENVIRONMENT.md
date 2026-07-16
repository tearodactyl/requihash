# ENVIRONMENT.md — RZ's package/build/link environment

What this crate actually needs on disk and in `PATH` to build and test,
captured once here rather than left implicit in `build.rs`. `build.rs` is
still the authoritative, executable description (this file can drift; that
can't, since `cargo` runs it every build) — this document exists so a human
can check prerequisites *before* running `cargo test` and get a readable
diagnosis if something's missing, and so `check_env.sh` (below) has a
single source of truth to implement against.

## What was actually used (this machine, recorded, not assumed)

- **OS/arch:** macOS 26.3, arm64 (Apple Silicon). Nothing in this crate's
  own Rust or C code is architecture-specific — no SIMD intrinsics, no
  `cfg(target_arch)` branches — so Linux/x86_64 should work unmodified, but
  has not been tried; if it doesn't, that's a real gap to fix, not an
  assumption to fall back on.
- **C compiler:** `/usr/bin/cc` → Apple clang 21.0.0 (`clang-2100.1.1.101`),
  via the Xcode Command Line Tools SDK. `build.rs` uses the `cc` crate's
  compiler *discovery* only (`cc::Build::get_compiler()`) — any C compiler
  the `cc` crate can find (clang, gcc, MSVC) should work; it has only been
  exercised against Apple clang.
- **Rust toolchain:** rustc 1.90.0, cargo 1.90.0. No specific edition/MSRV
  requirement was deliberately chosen — `Cargo.toml` declares `edition =
  "2021"`; no `rust-toolchain.toml` pins a specific version.

## Dependencies, and where each one actually comes from

Three categories — crates.io (ordinary, no action needed), a pinned local
registry cache (already present if `equihash-0.3.0` is a transitive
dependency anywhere on this machine, e.g. via Zebra), and one dependency
that is **not** fetched automatically and must exist locally before
`cargo build` will succeed:

1. **crates.io, resolved normally via `Cargo.lock`:**
   - `blake2b_simd = "1"` (locked to `1.0.4`) — the crate's own hashing
     dependency (the port's Rust BLAKE2b, independent of the two C-side
     BLAKE2b implementations below).
   - `cc = "1"` (build-dependency) — compiler discovery only, as above.
   - `serde`/`serde_json` (dev-dependencies) — JSON vector I/O in tests.
2. **The pinned `equihash` crate's vendored C source** — not a dependency
   of this crate's own `Cargo.toml` at all; `build.rs` locates it by
   globbing `~/.cargo/registry/src/index.crates.io-*/equihash-0.3.0/tromp/`
   directly off the *cargo registry cache*, independent of whether
   anything in `rz/`'s own dependency graph pulls that crate in. This
   works today because the crate is already cached locally (this project's
   other work, e.g. Zebra's own `equihash = "0.3"` pin, put it there) —
   **on a fresh machine with no prior cargo activity touching that crate,
   this glob will find nothing and `build.rs` will panic.** `RZ_EQUIHASH_TROMP_DIR`
   is the escape hatch: set it to point at any local checkout of that
   `tromp/` directory (e.g. a `cargo vendor` output, or the crate source
   extracted from its `.crate` tarball) to bypass the registry-cache glob
   entirely.
3. **The reference BLAKE2b implementation — the repository's vendored
   copy** (`../../../BLAKE/vendor/blake2`, repo-relative; provenance and
   update procedure in its `PROVENANCE.md`). `RZ_BLAKE2_REF_DIR`
   overrides it. The previous hardcoded `$HOME` path to a ZKs clone —
   flagged here as the one environment dependency worth fixing — was
   resolved 2026-07-16 by exactly the vendoring option this note
   proposed.

## What `build.rs` actually does, in build order

1. Resolve the vendored `equihash-0.3.0/tromp/` directory (registry glob,
   or `RZ_EQUIHASH_TROMP_DIR` override).
2. Assert `equi_miner.c` exists there.
3. Assert the vendored `BLAKE/vendor/blake2/blake2b-ref.c` exists
   (repo-relative; `RZ_BLAKE2_REF_DIR` overrides).
4. For each of three `(WN, WK, RESTBITS)` triples — `(200,9,9)`,
   `(200,9,8)`, `(144,5,4)` — invoke the discovered C compiler directly
   (not via `cc::Build`'s own archive-producing `compile()`/`try_compile()`,
   since those only ever produce a static library; this needs a linked
   executable) on three sources: the vendored, **unmodified**
   `equi_miner.c`; this crate's own `cross_check_c/harness_main.c`; and
   the reference `blake2b-ref.c` — with `-DWN=<n> -DWK=<k>
   -DRESTBITS=<r>`, include paths for all three source directories, `-O2`,
   warnings suppressed. Output: `$OUT_DIR/rz_xcheck_<WN>_<RESTBITS>`.
5. Export each binary's path via `cargo:rustc-env=RZ_XCHECK_BIN_<WN>_<RESTBITS>=...`
   so `tests/cross_check.rs` can find and invoke it without hardcoding
   `$OUT_DIR` itself.

## Reproducing outside `cargo` (`check_env.sh`)

`check_env.sh` in this directory checks the same three prerequisites
`build.rs` checks (a C compiler exists, the vendored crate is reachable,
the BLAKE2 reference clone exists) and reports plainly which one is
missing, without needing a full `cargo build` cycle to surface it. It does
not build anything itself — `build.rs` remains the one real build
description; this script exists purely to make a missing prerequisite
diagnosable in one step instead of via a `cargo` panic message.
