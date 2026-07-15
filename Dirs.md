# Dirs.md — directory map for Req/Requihash work

Scope: active work is `Req/` under this directory. This file maps other
local directories for fast lookup. Read/grep anywhere freely. Do not edit
files outside `Req/` (or this directory's top-level docs) without asking
first, every time.

## Relationship map

- Zebro (Rust node rewrite, migrating off Zcash/Zero's C++ lineage) needed a
  PoW evidence base for its D3 decision → drove the Equihash review in this
  directory (`Equihash.md`, `SOLVERS.md`) → surfaced the regularity repair
  (Requihash) and solver optimization/portability gaps → `Req/` is the
  resulting reference implementation, feeding back into Zebro's M3 package.
- Zero400/ZeroPerf are the actual production chain: release candidate 4.0.1,
  incremental C++, stock Equihash as deployed, **not** Rust/Zebra-code-compatible.
  Not derived from this work; tracked separately.
- ZKs is a neutral reference shelf: cloned upstream repos + project-agnostic
  survey docs, used by all of the above.

| Directory | Role | Contains |
|---|---|---|
| `~/Work/ZK/ZKs/` | Neutral reference shelf | Cloned coin/tooling repos, project-agnostic docs |
| `~/Work/ZK/Zebro/` | Origin/consumer | Rust node rewrite; this work's evidence base and destination |
| `~/Work/ZK/Zero400/` | Production chain | Release candidate 4.0.1, C++, stock Equihash |
| `~/Work/ZK/ZeroPerf/` | Production perf branch | Testing/benchmarking added on top of Zero400 |
| `~/Work/ZK/Repos/` | Zero org repo inventory | `ZeroC.md`/`ZeroC.csv` |

## 1. ZKs

Docs:
- `EquihashSurvey.md` — neutral algorithm/history/parameter survey. Distinct
  file from this directory's own `Equihash.md` (deeper Topic-A findings,
  F-A1–F-A11); cite `Equihash.md` for Requihash-specific claims.
- `Comparison.md` — cross-implementation comparison, stable numbered
  sections. §3 PoW (~line 109), §13 mining interfaces (~line 756), §12
  indexer/explorer services.
- `ZKRepos.md` — path index + clone-depth table for all cloned repos.
  `zebra` is shallow (1 commit, 2026-07-12) — no `git log`/blame; use GitHub
  API instead (`SOLVERS.md` did this for tromp/equihash).

Equihash/PoW file locations, verified:

| Repo | Remote | Files |
|---|---|---|
| `zcash` | `zcash/zcash` | `src/crypto/equihash.{h,cpp,tcc}`, `src/pow.{h,cpp}`, `src/rust/src/equihash.rs`. Canonical C++ reference. `chainparams.cpp:99,468`=(200,9) main/test, `:795`=(48,5) regtest |
| `zebra` | `ZcashFoundation/zebra` | `zebra-chain/src/work/equihash.rs` (309 lines, `Solution::check`/`solve`). Model for Zebro's verifier seam. Shallow clone, no history |
| `pirate` | `PirateNetwork/pirate` | `src/crypto/equihash.*`, `src/pow.{h,cpp}`. `chainparams.cpp:159,399`=(200,9), `:501`=(48,5). Cited in ZeroPerf `Perf.md` §0.1 as `BatchValidator` port precedent |
| `fluxd` | `RunOnFlux/fluxd` | `src/crypto/equihash.*`, `src/pow.{h,cpp}` |
| `zclassic` | `ZclassicCommunity/zclassic` | `src/crypto/equihash.*`, `src/pow.{h,cpp}`. Pre-Sapling zcashd-fork baseline |
| `zen` | `HorizenOfficial/zen` | `src/crypto/equihash.*`, `src/pow.{h,cpp}`. Legacy shielded-removal fork |
| `ycash` | `ycashfoundation/ycash` | `src/crypto/equihash.*`, `src/pow.{h,cpp}` |
| `ycash-zebra` | `ycashfoundation/zebra` | `zebra-chain/src/work/equihash.rs`. Second independent Rust reference |
| `hush3` | `git.hush.is/hush/hush3` | `src/crypto/equihash.*`, `src/pow.{h,cpp}`, `src/testequihash-cli`/`testequihashd`. Legacy alongside newer RandomX/RT_CST_RST PoW |
| `bitcoin-src` | `bitcoin/bitcoin` | `src/pow.{h,cpp}`. No Equihash (SHA256); non-Equihash PoW-interface baseline |
| `firo` | `firoorg/firo` | `src/pow.{h,cpp}`. No Equihash (own PoW + Spark privacy) |
| `blockbook` | `trezor/blockbook` | Go indexer. Not PoW; Req Group C integration target |
| `lightwalletd` | `zerocurrencycoin/lightwalletd` | Go light-client relay. Not PoW |
| `insight` | `tearodactyl/insight` | Zero Insight explorer stack. Not PoW |
| `PirateOcean` | `PirateNetwork/PirateOcean` | `src/crypto/equihash.*`, `src/pow.{h,cpp}`. Legacy ARRR wallet, superseded by `SevenSeas` |
| `SevenSeas` | `PirateNetwork/SevenSeas` | Qt wallet shell only, embeds `pirated` |
| `SilentDragon` | `MyHush/SilentDragon` | Qt wallet shell only |
| `safewallet` | `Fair-Exchange/safewallet` | Qt wallet shell only |
| `safecoin` | — | Not a repo itself; nested `safecoin/insight-api-safecoin`, `safecoin/Safecoin` |
| `TENT` | `TENTOfficial/TENT` | `src/crypto/equihash.*`, `src/pow.{h,cpp}`. Masternode-layer, relevant to Zebro ZeroNodeDev/TENT notes |
| `zerowallet-light-cli` | `zerocurrencycoin/zerowallet-light-cli` | Rust light-client CLI. Not PoW |

`Zeros` (no extension, ZKs top level) — plain-text Bitcoin Core multisig
tutorial, unrelated to the Zero coin.

Equihash solver implementations — these five clones are the ones
`SOLVERS.md` §0 and `Req/SOLVER_CORPUS.md` (RK/RZ/RT/CS's own source
locations) actually document and cite; not repeated here beyond a bare
locator, since both documents already give repo, remote, commit
provenance, and per-file analysis in more depth than a table in this
lookup file could without drifting out of sync:

- `equihash-khovratovich` (`khovratovich/equihash`) — RK's source.
- `equihash-tromp` (`tromp/equihash`) — RT's source (full multi-core
  `equi_miner.h`); also the pre-freeze ancestor of the single-core-stripped
  copy RZ ports.
- `equihash-xenon` (`xenoncat/equihash-xenon`) — index-pointer origin,
  analyzed in full in `SOLVERS.md` §0.2.
- `BTCGPU-equihash` (`BTCGPU/equihash`) — Bitcoin Gold's fork of tromp's
  solvers.
- `Generalized-Birthday-Problem` (`tl2cents/Generalized-Birthday-Problem`)
  — the 2025/1351 paper's own artifacts; CS's source.

Hash-primitive clones (blake2b/blake3, behind `Req/`'s Seam A — see
`Req/ARCHITECTURE.md` §1a): `blake2-reference` (`BLAKE2/BLAKE2`, the
official C reference, `neon/` NEON backend analyzed in `BENCHMARK.md` §7a/
PLAN.md A13), `blake2_simd` (`oconnor663/blake2_simd`, the Rust crate this
project and Zebra both depend on), `BLAKE3` and `BLAKE3-specs`
(`BLAKE3-team/BLAKE3` and its spec repo). Full citations for all of
these: `PAPERS.md` §10 / `Equihash.md` §9.

Reach into these clones for: real `(n,k)`/personalization cross-checks
(grep `chainparams.cpp` directly, don't trust memory); a second independent
Rust verifier (`zebra`/`ycash-zebra` vs. Req's own `rust/src/lib.rs` and
`zcash/src/rust/src/equihash.rs`); comparing Req's C++ against zcashd's actual
in-tree fork (`zcash/src/crypto/equihash.{h,cpp,tcc}`, forked at `690fc5eff`,
never resynced — `SOLVERS.md`). `Req/PLAN.md` A6 (Req's own future
production index-pointer solver) has no existing clone to copy from —
genuinely unbuilt work, distinct from RK/RZ/RT's standalone historical
ports above, which do have real upstream sources.

## 2. Zebro

Entry point: `~/Work/ZK/Zebro/ZEBRO.md` §1 (restart procedure).

- `Req/PLAN.md` Group C (C1: Req mines cross-parameter vectors →
  `zebro-bench` verify curve) — not started either side.
- Zebro's M3 evidence package needs `zebro-bench` curves across PoW
  parameter sets — Req's own counting-allocator memory harness
  (`rust/src/bin/req_memcheck.rs`) is built and measured, but only up to
  (96,5) (`Req/SIZING.md` §2a); extending past that is `Req/PLAN.md` Q2,
  a prerequisite for M3, not just adjacent.
- Zebro `ARCHITECTURE.md` uses the same swap-seam pattern as
  `Req/ARCHITECTURE.md` for hash/solve/verify backends.

## 3. ZeroPerf

Active `zerocurrencycoin/Zero` C++ branch (`perf-401`), testing/benchmarking
work on top of the Zero400 production chain. Not Rust/Zebra-compatible.
Currently mid-investigation on Groth16 batch-verification (`Perf.md` §0.1,
open blocking decision). Not about Equihash/Requihash directly, except:

- `ZeroPerf/ZERO_COIN.md:26` — Zero's live mainnet params: Equihash (192,7).
  Motivated the (192,7) row in `Req/SIZING.md`.
- `ZeroPerf/TODO.md` Active — "Equihash (192,7) test vectors," unstarted.
  Same parameter point `Req/SIZING.md` covers theoretically; not wired
  together.
- ZeroPerf's own Equihash source is presumably `src/crypto/equihash.*` /
  `src/pow.{h,cpp}` (same zcashd-derived layout as the ZKs clones) — not
  cross-checked against Req.

## 4. Zero400 / Repos

- `Zero400/ZERO_COIN.md` — release candidate 4.0.1 chain reference. Compare
  against ZeroPerf's copy (more actively maintained) before trusting either
  as current.
- `Repos/ZeroC.md` / `ZeroC.csv` — org-wide repo inventory for
  `zerocurrencycoin`. Maintainer `tearodactyl` = this project owner's own
  GitHub handle (confirmed via `tromp/equihash#19`).
