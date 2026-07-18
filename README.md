# Requihash

Requihash is a project to build a new implementation of a proof-of-work
verifier and miner in the Equihash lineage, informed by a decade of deployment
history and by recent general research. Equihash was designed for ASIC
resistance via memory-hardness, and lost it in 2016-17 to a single structural
shortcut — the single-list index-pointer optimization — that the theory of the
time did not anticipate. The 2025 regularity result (Tang, Sun, Gong, "On the
Regularity of the Generalized Birthday Problem") explains the gap and supplies
a minimal repair: draw each solution index from a distinct list class,
restoring the regular k-list problem Wagner's algorithm was actually designed
for. This project turns that repair, and the broader design space around it,
into working, measured, cross-validated code.

[Equihash.md](Equihash.md) tells the history and theory end to end (findings
F-A1..F-A11); [SOLVERS.md](SOLVERS.md) is the primary-source solver history;
[PAPERS.md](PAPERS.md) covers the 2025 literature; [UNIHASH.md](UNIHASH.md) is
a research-only unifying parametrization (not adopted); [Dirs.md](Dirs.md)
maps how this directory relates to the surrounding repos (Zebro, ZKs
reference clones, Zero400/ZeroPerf) and which are safe to edit.

## The three bodies of work

- **[Req/](Req/README.md)** — the integrated solution under development: a
  Rust verifier/miner synthesizing a BLAKE flavor with a parametrized
  generalized-birthday problem (`PoW(n, k, hash, m, keying, context)`,
  [Req/SPEC.md](Req/SPEC.md)), aimed as a drop-in at the Zcash/Zebra-style
  verifier seam for the Zebro node revamp — with a byte-exact C++ twin as
  its differential oracle.
- **[BLAKE/](BLAKE/BLAKE.md)** — compatible and performant BLAKE2/BLAKE3
  support across platforms: UniBlake, a unified C/C++ BLAKE2b/3 with runtime
  CPU dispatch, forced-implementation override, self-test gating, and
  measured (not assumed) SIMD kernels ([BLAKE/UniBlake.md](BLAKE/UniBlake.md),
  [BLAKE/Platforms.md](BLAKE/Platforms.md)).
- **[Req/SOLVER_CORPUS/](Req/SOLVER_CORPUS.md)** — solver experiments in C
  and Rust across code provenances — original authors' reference
  (RK/Khovratovich), tromp's miners (RZ stripped, RT full multi-core),
  Sequihash (CS, C++ and Rust) — mixing reference, third-party, vendored,
  and authored code and wrappers to build full command of the algorithmic
  details, validation discipline, and optimization techniques.

## Quickstart

    cd Req/rust && cargo test                 # Rust: solve+verify, KATs, rejection matrix
    cmake -S Req/cpp -B Req/cpp/build && cmake --build Req/cpp/build && Req/cpp/build/req_test
    Req/cpp/build/req_gen Req/vectors && (cd Req/rust && cargo run --bin req_xcheck -- ../vectors)

The third line is the cross-check: solutions mined by the C++ implementation,
verified by the independent Rust one, byte-exact.

## Where to go, by user type

- **Newcomer**: read [Equihash.md](Equihash.md)'s Findings section for the
  story, then [Req/README.md](Req/README.md) for the implementation.
- **Experimenter (power user)**: the quickstart above, then
  [Req/SIZING.md](Req/SIZING.md) §4 for the parameter tiers (trace/debug →
  CI/testnet → memory-hard), `Req/rust/bench.sh` for the standard bench run,
  [Req/SOLVER_CORPUS.md](Req/SOLVER_CORPUS.md) for the runnable historical
  ports, and [BLAKE/uniblake/](BLAKE/uniblake/) for the hash-kernel PoC.
- **Cryptocurrency integrator**: [Req/SPEC.md](Req/SPEC.md) for the frozen
  wire format, [Dirs.md](Dirs.md) for how this relates to Zebro and the
  Zero/Zcash lineage repos, [Req/PLAN.md](Req/PLAN.md) for what is done vs.
  pending.
- **Algorithm researcher**: [SOLVERS.md](SOLVERS.md) (primary-source solver
  history), [Req/SECURITY_ANALYSIS.md](Req/SECURITY_ANALYSIS.md) (attack
  surface, TMTO program), [PAPERS.md](PAPERS.md) (2025 literature),
  [Req/BENCHMARK.md](Req/BENCHMARK.md) / [Req/SIZING.md](Req/SIZING.md)
  (evidence-graded numbers; check the grade before citing).
