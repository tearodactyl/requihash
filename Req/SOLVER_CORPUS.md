# SOLVER_CORPUS.md — comparative solver/verifier ports (RK, RZ, RT, CS)

Purpose: build a body of solver and verifier implementations spanning the
historical lineage (original authors → the pinned Rust verifier ecosystem
→ tromp's optimized C solver → the paper's own Sequihash reference) to
gain hands-on expertise with each design and to measure real hardware
parameters (memory, timing) against implementations this project did not
write, as a check on `Req/`'s own numbers. Each task below is scoped for
standalone execution with no other context needed from this repository's
other documents. Full commit-level provenance and history for the
tromp/equihash lineage (RZ and RT's sources) is not repeated here — see
`~/Work/ZK/Requihash/SOLVERS.md`, which already covers it in depth.

**Cross-cutting requirements for all four tasks:**

- Keep the algorithm itself (the solver/verifier logic being ported)
  structurally separate from any harness, CLI, or validation-wrapper
  code — a port's core module should be usable as a library with no
  console/profiling/argument-parsing dependency, matching the separation
  already established in `Req/rust/src/` (algorithm in
  `lib.rs`/`solve/`/`verify/`, CLI/harness code in `src/bin/`). Vectors
  and other generated validation artifacts (KAT sets, differential-fuzz
  logs) belong in their own directory or crate, not mixed into the
  algorithm's own source tree.
- **Specific file/component/module/function compatibility with the
  starting implementation is NOT required**, except where needed for
  clarity and continuity (e.g. so a reviewer can trace a ported function
  back to its source). Use idiomatic Rust or C++ conventions, apply a
  more logical and natural partitioning/layout than the original where
  one is obvious, and drop the original developer's idiosyncratic
  naming/structuring choices where they don't serve the port — these are
  all encouraged, not something to avoid for the sake of a 1:1 mapping.
  What each task's "byte-exact"/"validation" requirements bind you to is
  *output* equality (index sets, encodings, as specified per task) — not
  structural mimicry of the source's internal shape.

## References

- Biryukov, Khovratovich, "Equihash: Asymmetric Proof-of-Work Based on the
  Generalized Birthday Problem," NDSS 2016 —
  [paper](https://www.internetsociety.org/sites/default/files/blogs-media/equihash-asymmetric-proof-of-work-based-generalized-birthday-problem.pdf),
  [reference implementation](https://github.com/khovratovich/equihash)
  (local clone `~/Work/ZK/ZKs/equihash-khovratovich`).
- `tromp/equihash` — [repo](https://github.com/tromp/equihash) (local
  clone `~/Work/ZK/ZKs/equihash-tromp`, full history); vendored pinned
  snapshot inside the `equihash` crate,
  `~/.cargo/registry/.../equihash-0.3.0/tromp/` (frozen at commit
  `690fc5eff`, provenance in `~/Work/ZK/Requihash/SOLVERS.md`). RT's
  source; RZ's source (the same crate's `verify.rs`/`minimal.rs`) is
  Rust-native, not part of tromp's C code, despite sharing a crate.
- `nicehash/nheqminer`, `cpu_tromp/` — [repo](https://github.com/nicehash/nheqminer)
  (local clone `~/Work/ZK/ZKs/nheqminer`), a second copy of tromp's
  multi-threaded solver, useful as a cross-check that the full pthread
  design in `~/Work/ZK/ZKs/equihash-tromp` is not a one-off. Referenced
  only as context for RT's scope decision, not a port target for any
  task in this document.
- `ZcashFoundation/zebra`, `zebra-chain/src/work/equihash.rs` — local
  clone `~/Work/ZK/ZKs/zebra`. Explicitly **not** a port target for RZ or
  any task here — confirmed by reading it that it only wraps the pinned
  `equihash` crate, with no independent algorithm of its own. Referenced
  so RZ's "why not Zebra" reasoning can be checked directly rather than
  taken on faith.
- Tang, Sun, Gong, "On the Regularity of the Generalized Birthday Problem,"
  eprint 2025/1351 — [paper](https://eprint.iacr.org/2025/1351),
  [artifact repo](https://github.com/tl2cents/Generalized-Birthday-Problem)
  (local clone `~/Work/ZK/ZKs/Generalized-Birthday-Problem`; the paper's
  own name for this construction is "Sequihash").
- `Req/SPEC.md` — this project's own normative spec, cited throughout for
  parameter/encoding conventions each port is compared against.
- `UNIHASH.md` — the parametrization proposal unifying Equihash/Requihash/
  Sequihash across `keying`/`encoding` axes; relevant context for CS
  specifically (see CS's own section below), not required reading for RK,
  RZ, or RT.

---

## RK

### Task

Port Khovratovich's canonical C++ reference solver into Rust, as a
standalone crate/binary — not inside `Req/rust/src/solve/`, since `Req/`'s
own `reference.rs` already independently implements the same naive Wagner
walk; RK's value is a faithful line-for-line port of this specific
historical artifact, not another from-scratch naive solver. Port the
`Equihash` class's five methods (`InitializeMemory`, `FillMemory`,
`ResolveCollisions`, `FindProof`, `ResolveTree`) preserving their
structure and naming where idiomatic Rust allows.

Keep the ported algorithm (leaf generation, tree-fold, proof extraction)
in its own module with no CLI/console dependency; any command-line driver
or benchmark harness is a separate binary calling into that module.

#### Parameters

Matches the original exactly — `n ≤ 32 bytes` (256 bits), any `k` where
the `Seed`/`LIST_LENGTH` constants hold. No hardcoded `(n,k)`
combinations: the original is a generic recursive tree-fold over
dynamically-sized `Tuple` vectors (`std::vector<std::vector<Tuple>>`),
not bucket-specialized per-parameter C (that is tromp's design, RT, not
this one). State this parameter range explicitly in the port's own
docs — RK's range is *broader* than RT's by construction, and it is easy
to assume the opposite (that the "older" implementation is more limited).

**Foundation to expand parameters toward Zero/Zcash values**: the
original's own README recommends small families — cryptocurrency
`(100/110/120,4)`, `(108/114/120/126,5)`; client-puzzle `(60/70/80/90,4)`,
`(90/96/102,5)` — all `k ∈ {4,5}`. Because the port is parameter-generic
(no compile-time specialization to extend), reaching `k=7` (Zero
Currency's real mainnet parameter, `(192,7)`) and `k=9` (Zcash's
`(200,9)`) requires no new port work at all — only new vector points at
those `(n,k)` values, generated the same way as the smaller ones. This
should be built into the vector set from the start (see Validation below)
rather than treated as a later extension, since the port itself places no
`k`-specific restriction to work around.

#### Validation

Vectors, not a live subprocess dependency on the original. The original is
CC0 C++11, buildable via its own `Makefile` — build it once, generate a
fixed KAT set (input/nonce/solution triples) at the small recommended
families above plus `(192,7)` and `(200,9)` per the expansion note, commit
those as vectors in their own directory (mirroring `Req/vectors/`'s
schema, but kept in this port's own crate, not mixed into `Req/`'s), and
validate the Rust port against the vectors — not a permanent runtime
dependency on a 2016-era C++11 build.

Byte-exact target: the solution (index set), not a wire encoding. The
original defines no minimal/compressed encoding at all — it emits raw
index arrays. Validate on raw index-set equality first; only additionally
check against `Req/`'s own `get_minimal_from_indices` if a wire-format
comparison is separately wanted — two separable claims, do not conflate
them in the port's test suite.

**Exit criteria** (subsumed here, not a separate section): Rust port
produces byte-identical index sets to the vectors generated from the
original C++, across every `(n,k)` point vectored, including `(192,7)`
and `(200,9)`; the port's own README states the parameter range and the
"broader than RT" fact explicitly; no wire-encoding claim made without a
separately-labeled test for it.

### Original

`~/Work/ZK/ZKs/equihash-khovratovich/Source/C++11/{pow.h,pow.cc}` (117 +
218 lines). CC0-licensed, no attribution required.

### RK Prompt

> You are porting a specific, small C++ reference implementation to Rust.
> Do not redesign it; translate it faithfully, then validate.
>
> **Source**: `~/Work/ZK/ZKs/equihash-khovratovich/Source/C++11/pow.h` and
> `pow.cc` (117 + 218 lines). Read both files in full before writing any
> Rust.
>
> **Target layout**: new directory `~/Work/ZK/Requihash/SOLVER_CORPUS/rk/`
> — `src/lib.rs` (the ported `Equihash` class: `InitializeMemory`,
> `FillMemory`, `ResolveCollisions`, `FindProof`, `ResolveTree`, as a
> library with no I/O, no CLI, no println), `src/bin/rk_gen.rs` (a
> separate binary that drives the library to produce KAT vectors),
> `tests/` (the validation harness), `vectors/` (generated JSON, gitignored
> from the crate's own build artifacts but committed as data).
>
> **Steps, in order**:
> 1. Build the original C++ from its own `Makefile`, confirm it runs.
> 2. Port the `Equihash` class to `src/lib.rs`. Preserve method names and
>    control flow; translate `std::vector<std::vector<Tuple>>` to
>    idiomatic `Vec<Vec<Tuple>>` or equivalent — do not introduce a
>    different algorithm.
> 3. Generate KAT vectors by running the built C++ binary at these exact
>    `(n,k)` points: `(100,4)`, `(120,4)`, `(108,5)`, `(126,5)` (the
>    original README's own recommended families), plus `(192,7)` and
>    `(200,9)` (no port change needed for these — the algorithm is
>    parameter-generic). Each vector: `{n, k, seed, indices}` — no
>    encoding field, raw index array only. Write one JSON file per
>    `(n,k)` point to `vectors/`.
> 4. Write `tests/cross_check.rs`: for each vector file, run the Rust
>    port with the same `(n, k, seed)` and assert the returned index set
>    equals the vector's `indices` (as sets, order-independent unless the
>    original itself sorts — check `pow.cc` to confirm which).
> 5. Write `README.md` in `rk/`: state the parameter range (`n ≤ 32
>    bytes`, any `k`), state explicitly that this is broader than the RT
>    port's range (RT is compile-time-restricted to specific `(WN,
>    RESTBITS)` pairs), state that no wire/minimal encoding is validated
>    (the original defines none).
>
> **Stop and report if**: any vector point fails to match, or the
> original C++ fails to build. Do not silently skip a failing point.
>
> **Done means**: `cargo test` passes in `rk/`, all 6 vector points
> present and matched, `README.md` written with both required statements.

---

## RZ

### Task

Port the pinned `equihash` crate's Rust-native verifier — **not** Zebra's
own `zebra-chain::work::equihash::Solution`, which is a thin wrapper
around this same crate and contains no independent algorithm. Confirmed
by reading both: `Solution::check` (`zebra-chain/src/work/equihash.rs`)
and Zebra's `Solution::solve` both delegate directly to the pinned
`equihash` crate (`is_valid_solution` and `tromp::solve_200_9`
respectively) — there is nothing Zebra-specific to port. The real,
independent, single-core Rust algorithm is the `equihash` crate's own
`verify.rs`/`minimal.rs` (tree-fold verifier, `Node`/`from_children`
collision checking), which is what RZ actually targets.

This makes RZ a verifier port, distinct in kind from RK (solver) and RT
(solver) — a natural complement to `Req/rust/src/verify/`, which already
has three verifier backends (`reference`, `arena`, `early`) cross-
validated against each other; RZ adds a fourth, external reference point
from a completely independent codebase.

#### Parameters

Fully parametric at runtime — `is_valid_solution(n, k, input, nonce,
solution)` takes `n`/`k` as plain arguments (`params.rs`'s own
`Params::new`, constraints `n%8==0`, `k>=3`, `k<n`, `n%(k+1)==0`). No
compile-time restriction of any kind, unlike RT's solver target. Reaching
`k=7`/`k=9` (Zero/Zcash values) needs no port work — only vector points at
those parameters, same situation as RK.

#### Validation

Vectors, generated from the crate itself (it is already a pinned Rust
dependency — no build step beyond `cargo`, no FFI, no subprocess). Use
the crate's own `is_valid_solution` directly as the oracle: feed it known
solutions (the official KAT set already pulled into this project at
`Req/vectors/zcash_kat_*.json` per `Req/PLAN.md` A14 is directly usable
here without re-generating anything — those vectors are exactly this
crate's own official test data) and known-invalid solutions
(`Req/vectors/zcash_kat_invalid.json`, same source).

Byte-exact target: the accept/reject decision and, where the invalid
vectors specify one, the specific error kind (`Kind::Collision`,
`Kind::OutOfOrder`, `Kind::DuplicateIdxs` per the crate's own `Kind` enum
in `verify.rs`) — not a wire encoding, since this is a verifier, not a
solver; there is no output to encode beyond the pass/fail result.

**Exit criteria** (subsumed here): Rust port accepts every vector in
`zcash_kat_96_5.json`/`zcash_kat_144_5.json`/`zcash_kat_200_9.json` and
rejects every vector in `zcash_kat_invalid.json` with a matching error
kind; the port's own README states explicitly that it targets the
`equihash` crate's verifier, not Zebra's wrapper, and why (the wrapper has
no independent algorithm).

### Original

`~/.cargo/registry/src/index.crates.io-*/equihash-0.3.0/src/verify.rs`
(318 lines) and `src/minimal.rs` (265 lines) — resolve the `*` glob to
the one matching directory on this machine.

### RZ Prompt

> You are porting a Rust verifier to Rust — this is a re-implementation
> exercise (independent code producing the same accept/reject decisions),
> not a translation from another language. Do not import or wrap the
> original crate; write independent code that reaches the same verdicts.
>
> **Source**:
> `~/.cargo/registry/src/index.crates.io-*/equihash-0.3.0/src/verify.rs`
> and the same directory's `minimal.rs`. Read both in full before writing
> anything. Do **not** read or port
> `~/Work/ZK/ZKs/zebra/zebra-chain/src/work/equihash.rs` — confirm for
> yourself first that it only wraps the crate above (check `Solution::check`
> and `Solution::solve`'s bodies), then treat it as out of scope.
>
> **Target layout**: new directory
> `~/Work/ZK/Requihash/SOLVER_CORPUS/rz/` — `src/lib.rs` (the
> re-implemented verifier: tree-fold collision checking, distinct-index
> and ordering checks; no I/O), `tests/` (uses existing vectors, generates
> nothing new).
>
> **Steps, in order**:
> 1. Read `verify.rs`'s `Node`/`from_children` tree-fold logic and
>    `minimal.rs`'s `expand_array`/`indices_from_minimal`. Identify the
>    four checks it performs (collision-per-round, ordering, distinctness,
>    zero-root — cross-check this list against what you actually find,
>    the crate's own `Kind` enum names the failure modes).
> 2. Write an independent Rust implementation reaching the same
>    accept/reject decisions. You do not need to structure it the same
>    way as the original — use whatever module/function layout is
>    clearest; the requirement is matching verdicts, not matching code
>    shape.
> 3. Point the test suite directly at this project's own existing vector
>    files: `~/Work/ZK/Requihash/Req/vectors/zcash_kat_96_5.json`,
>    `zcash_kat_144_5.json`, `zcash_kat_200_9.json` (must all be accepted)
>    and `zcash_kat_invalid.json` (must all be rejected, with the error
>    kind matching each vector's `expect_error_kind` field). Do not
>    regenerate these vectors — they already exist and are the crate's own
>    official KAT data.
>
> **Stop and report if**: any valid vector is rejected, or any invalid
> vector is accepted, or an invalid vector's actual error kind doesn't
> match its `expect_error_kind` field.
>
> **Done means**: `cargo test` passes in `rz/` against all four existing
> vector files with zero new vectors generated, `README.md` states this
> targets the `equihash` crate's verifier (not Zebra's wrapper) and why.

---

## RT

### Task

Port tromp's C index-pointer algorithm into Rust. **Before writing any
Rust**, resolve the scope question below — it changes the task
substantially and should not be decided implicitly by which source file
happens to be open.

#### The scope question: reduced vendored snapshot vs. full multi-core original

Two real source targets exist, and picking between them (or sequencing
both) is the actual first decision:

1. **The reduced, vendored snapshot** — inside the `equihash` crate
   (`~/.cargo/registry/.../equihash-0.3.0/tromp/equi_miner.c`, 737 lines).
   Single-threaded (multi-threading was stripped downstream at some
   point before vendoring; full history and exact commit in
   `~/Work/ZK/Requihash/SOLVERS.md`, not repeated here). This exact
   snapshot is what `Req/PLAN.md` A6/A18 already established as frozen at
   commit `690fc5eff`, so a port of *this version* has direct provenance
   value for A6's eventual production backend comparison.
2. **The full, multi-core original** — `~/Work/ZK/ZKs/equihash-tromp/equi_miner.h`
   (1160 lines) or `nheqminer`'s copy (`~/Work/ZK/ZKs/nheqminer/cpu_tromp/equi_miner.h`,
   629 lines — a different, also-real variant, confirms the pthread design
   isn't a one-off). Both use `pthread_barrier_t` for genuine multi-threaded
   bucket-round synchronization (`equi_miner.h` line ~1107's `worker()`
   function and the barrier-based round handoff) — the real, deployed
   concurrency design, not present in the vendored snapshot at all.

**Relative effort estimate, from reading both source trees directly (not
guessed):**

| | Reduced (single-threaded, vendored) | Full (multi-core, `equi_miner.h`) |
|---|---|---|
| Source size | 737 lines, one file | 1160 lines (or 629 in nheqminer's variant), same file family but with `pthread_barrier_t`, `thread_ctx`, and the `worker()` dispatch loop added |
| Port effort | Lower — direct translation of a single sequential control flow (`equi_digit0` → `equi_digitodd`/`equi_digiteven` per round → `equi_digitK`) into Rust, no concurrency primitives to translate | Meaningfully higher — the barrier-synchronized round handoff has no direct Rust equivalent to transliterate; a faithful port needs either a Rust barrier primitive (`std::sync::Barrier`) mapped onto the same per-round handoff, or a redesign onto `rayon`/scoped threads, which is a genuine concurrency-design decision, not a mechanical translation |
| Validation effort | Lower — single-threaded output is deterministic per nonce, trivially compared to a reference run | Higher — multi-threaded output must still be validated as deterministic (same solution set regardless of thread count/scheduling), which is an extra property to test beyond simple equality, and flaky-test risk if the port's synchronization has a bug |
| Provenance value for A6 | Direct — this is the exact frozen snapshot A6's production backend will be compared against | Indirect — the concurrency layer is orthogonal to A6's own `all_solvers()`/`solve::pointer` scope (concurrency is a separate, later step per `Req/ARCHITECTURE.md` §7's own staged plan, item 3, "bucket-addressing... only if profiling justifies it") |

**Recommendation: start with the reduced, single-threaded vendored
snapshot only. Treat the full multi-core port as a distinct, later,
separately-scoped task — do not attempt both in one pass.** The
indecision named in this document's own prior framing ("tempted to ask
for implementing both") is resolved by noting that the two targets answer
different questions: the reduced snapshot directly serves A6's near-term
provenance need with the lower-effort, lower-risk port; the full
multi-core version is a genuine concurrency-design exercise whose value
depends on a not-yet-decided future application (a real parallel
production backend, which `Req/PLAN.md` A9's Q1 concurrency audit is
already separately investigating for `Req/`'s own bucket solver, without
reference to tromp's specific barrier design). Sequencing the multi-core
port as an explicit follow-on, once the reduced port's validation
approach is proven and *if* a concrete need for tromp's specific
threading design (rather than a Rust-native concurrency design) emerges,
avoids committing effort to a concurrency port before knowing whether its
specific design is even the one worth matching.

Keep the ported algorithm (the bucket/collision-round logic) in its own
module with no CLI/console dependency, same as RK.

#### Parameters

Tromp's C solver is **not parametrically general** — it is specialized
per `(WN, BUCKBITS, RESTBITS)` combination via compile-time
`#if`/`#elif`/`#error` branches in `getxhash0`/`getxhash1` and the two
bucket-ID computation functions. The vendored `equi_miner.c` only
implements: `WN=200` with `RESTBITS ∈ {4,8,9}`, `WN=144` with
`RESTBITS=4`, and `WN=96` with `RESTBITS=4` — anything else fails at
**compile time**, not runtime. This is fundamentally different from RK's
original, which is parameter-generic at runtime.

The Rust port must pick one of two options explicitly, not blur them:
(a) **mirror the same compile-time specialization** (Rust `const`
generics or a macro expanding the same fixed `(n, restbits)` set) —
faithful to the original's actual design, equally limited; or
(b) **generalize the bit-extraction logic** to work for arbitrary `(n,
restbits)` — a genuine *improvement*, and must be labeled as an
improvement, not presented as "the same algorithm," if done. Given the
recommendation above (reduced snapshot first), option (a) is the lower-
risk starting choice — it validates the port against a known-fixed
parameter set before any generalization risk is introduced; (b) can be a
follow-on once (a) is proven, at which point it also naturally reaches
`k=7`/`k=9` (Zero/Zcash values) if the generalized bit-extraction is
correct for those `RESTBITS`.

#### Validation

Running the original source directly is viable and preferred over
vectors alone — the vendored crate is a stable, versioned dependency
(`equihash-0.3.0`), not a shallow clone with drift risk, and this project
already has a working precedent for pulling KATs from this exact crate
(`Req/PLAN.md` A14). Build the pinned C via the crate's own build script
(or a thin CMake wrapper) as a cross-check binary, generate vectors from
it the same way A14 did, kept in their own directory separate from the
port's algorithm module — *and* keep the option to invoke the C binary
directly for a larger differential fuzz pass (many random nonces) beyond
what a fixed vector set covers: vectors for regression, live execution
for one-time deep validation.

Byte-exact target: the compressed-pair wire encoding, not just the index
set. Tromp's triangular-number packing (`x = b(b-1)/2 + s`, mirrored in
`tree_from_bid`) is closer to what `Req/`'s own future `solve::pointer`
production backend (A6) will need to be validated against — RT is the
more directly useful of the two ports for A6, not just an
expertise-building exercise. State this dependency explicitly in RT's own
docs.

**Exit criteria** (subsumed here): Rust port produces byte-identical
solutions to the pinned C source at every `(WN, RESTBITS)` combination
the reduced snapshot supports; the compile-time-specialization-vs-
generalization choice is stated explicitly, not left ambiguous; the
compressed-pair encoding is validated against the original's own
encoding, not merely the index set; the full multi-core port is
explicitly out of scope for this pass and recorded as a separate future
task, not attempted partially.

### Original

Reduced (start here): the vendored, pinned copy inside the `equihash`
crate, `~/.cargo/registry/.../equihash-0.3.0/tromp/equi_miner.c`. Full
multi-core (separate future task): `~/Work/ZK/ZKs/equihash-tromp/equi_miner.h`
or `~/Work/ZK/ZKs/nheqminer/cpu_tromp/equi_miner.h`.

### RT Prompt

> You are porting one specific C file to Rust. A second, larger source
> exists (tromp's multi-threaded original) — do not touch it, do not
> read it beyond confirming it exists. Scope is the reduced snapshot only.
>
> **Source**: `~/.cargo/registry/src/index.crates.io-*/equihash-0.3.0/tromp/equi_miner.c`
> (737 lines, single file, single-threaded — resolve the `*` glob to the
> one matching directory on this machine). Read it in full, plus its
> sibling `equi.h` in the same directory, before writing any Rust.
>
> **Target layout**: new directory
> `~/Work/ZK/Requihash/SOLVER_CORPUS/rt/` — `src/lib.rs` (the ported
> bucket/collision-round algorithm as a library, no I/O), `src/bin/
> rt_gen.rs` (drives the C reference via FFI or a subprocess to build
> vectors — your choice, state which), `tests/`, `vectors/`.
>
> **Steps, in order**:
> 1. Find every `#if`/`#elif`/`#error` branch in `getxhash0`, `getxhash1`,
>    and the two bucket-ID computation functions. List the exact
>    `(WN, RESTBITS)` pairs that compile (expected: `WN=200` with
>    `RESTBITS ∈ {4,8,9}`; `WN=144` with `RESTBITS=4`; `WN=96` with
>    `RESTBITS=4` — confirm this against the actual source, do not trust
>    this list blindly).
> 2. Port the algorithm choosing **option (a)**: mirror the same
>    compile-time specialization using Rust `const` generics or a macro
>    over the same fixed `(WN, RESTBITS)` set found in step 1. Do not
>    generalize to arbitrary parameters — that is out of scope for this
>    pass. State this choice as the first line of `rt/README.md`.
> 3. Build the pinned C source as a cross-check binary (a thin `build.rs`
>    or CMake wrapper compiling `equi_miner.c` directly — do not modify
>    the C source).
> 4. Generate vectors at every `(WN, RESTBITS)` pair from step 1: for each,
>    run the C binary at 3 distinct nonces, capture the full solution
>    (index set) **and** the compressed-pair intermediate encoding
>    (`tree.bid_s0_s1`'s packed form, `x = b(b-1)/2 + s`) if the C source
>    exposes it directly — if it does not expose the intermediate
>    encoding without modifying the C, say so explicitly in the report
>    rather than modifying the pinned source to extract it.
> 5. Write `tests/cross_check.rs`: assert the Rust port's output matches
>    the C binary's output at every vectored point, for both the index
>    set and (if captured) the compressed-pair encoding.
>
> **Stop and report if**: the `#if` branches found in step 1 don't match
> the expected list above (report the actual list, don't force-fit);
> extracting the compressed-pair encoding requires modifying the pinned
> C source (report this as a blocker, don't modify the source to work
> around it).
>
> **Done means**: `cargo test` passes in `rt/` for every `(WN, RESTBITS)`
> pair found in step 1, `README.md` states the option-(a) choice and the
> full-multi-core-port-is-a-separate-task note, and the report states
> explicitly whether the compressed-pair encoding was validated or only
> the index set was.

---

## CS

### Task

Write a canonical C++ implementation matching the paper's own Python
"Sequihash" reference solver. Two real, load-bearing incompatibilities
exist between this reference and `Req/`'s own conventions — found by
reading the Python source directly, not assumed from the paper's prose —
and must be resolved or explicitly documented, not silently papered over:

1. **Parameter convention differs.** The Python reference's `k`
   (`k_list_wagner_algorithm.__init__`, asserting `k` is a power of 2) is
   the paper's own `K` — a *list count* — matching the paper's `(n,
   K=2^k)` table directly, **not** `Req/`'s `(n,k)` where `k` is
   tree-depth and `2^k` is the *solution size*. `Req/README.md`'s "What
   Requihash changes" section already documents the equivalent
   distinction for Requihash vs. Equihash's convention; CS needs the same
   explicit note.
2. **Leaf/nonce encoding differs.** The Python reference hashes `nonce +
   f"{i}-{j}".encode()` — an ASCII decimal string like `b"3-42"` appended
   to a 16-byte nonce — not `Req/`'s binary `le32(i mod k) ||
   le32(i div k)`. A C++ port aiming for byte-exact compatibility must
   reproduce the **string formatting** (decimal digits, no leading zeros,
   no fixed width, the literal `-` separator), not translate it into a
   binary encoding. `UNIHASH.md` proposes that this difference is a
   non-cryptographic, likely-accidental artifact (see `UNIHASH.md` §1)
   rather than a principled design choice — worth reading for context on
   *why* the incompatibility exists, but this task's scope (a faithful
   C++ port of the Python reference as it actually is) is unchanged by
   that proposal.

"Canonical" here means faithful to the algorithm, not the Python source's
incidental scaffolding: the reference mixes `rich`/`tracemalloc`
console-output/profiling code directly into the algorithm class
(`run_with_memory_trace`, `Console`/`Panel` imports) — the C++ port must
separate the algorithm (hash-merge, list generation, solve) from this
presentation/profiling scaffolding into its own module, with no
console/profiling dependency, matching this document's cross-cutting
requirement.

#### Parameters

Matches the Python reference's own convention (`n`, `K` a power of 2) —
see incompatibility 1 above. The reference itself does not restrict `K`
beyond the power-of-2 assertion, so reaching `K` values corresponding to
Zero's `k=7`/Zcash's `k=9` (in Requihash's tree-depth convention) is a
matter of running the reference and the port at those points, not a
structural limitation to work around — same situation as RK, unlike RT.

#### Validation

Both vectors and live execution, for different purposes. Vectors:
generate a fixed KAT set by running the actual Python reference
(`k_list_wagner_algorithm.new(n, k, nonce).solve()`) at small `(n,K)`
points, kept in their own directory in a new schema (this is Sequihash's
raw `(hash_value, index_vector)` output, pre-encoding — not `Req/`'s wire
format; do not reuse `Req/vectors/`'s schema unchanged). Live execution:
the Python reference is confirmed genuinely runnable (`Req/SIZING.md` §0
documents executing it once this project, at a tiny parameter point,
self-verifying one solution) — a differential harness running the Python
reference and the C++ port side-by-side on many random nonces is feasible
and more convincing than a fixed vector set alone at these small, fast
parameter sizes.

Byte-exact target: the index vector only. There is no minimal/compressed
wire encoding in the paper's own artifact to match — if CS wants a
compact encoding downstream, that is a CS-specific design decision
layered on top, not something to reverse-engineer from the Python
reference (it defines none).

**Exit criteria** (subsumed here): C++ port produces byte-identical index
vectors to the Python reference (both the vectored KATs and, ideally, a
live differential fuzz pass) at every `(n, K)` point checked, including
values reaching `k=7`/`k=9`-equivalent `K`; both load-bearing
incompatibilities are stated explicitly in the port's own README, not
left implicit; the profiling/presentation-scaffolding separation is done
(algorithm code has no `rich`/console dependency).

### Original

`~/Work/ZK/ZKs/Generalized-Birthday-Problem/GBP-solver/k_list_algorithm.py`
(273 lines) — the 2025/1351 paper's own runnable k-list Wagner solver.

### CS Prompt

> You are porting a Python research reference to C++. The reference mixes
> profiling/console code into the algorithm — strip that, port only the
> algorithm. Two naming/encoding conventions differ from this project's
> own Rust/C++ code; reproduce the Python reference's conventions
> exactly, do not silently translate them to match this project's style.
>
> **Source**:
> `~/Work/ZK/ZKs/Generalized-Birthday-Problem/GBP-solver/k_list_algorithm.py`
> (273 lines). Read it in full before writing any C++. Confirm these two
> facts yourself by finding the exact lines: (1) the constructor's `k`
> argument is asserted to be a power of 2 and represents a *list count*,
> not a tree depth; (2) `compute_item(i, j)` hashes
> `self.nonce + f"{i}-{j}".encode()` — an ASCII string, not a binary
> encoding.
>
> **Target layout**: new directory
> `~/Work/ZK/Requihash/SOLVER_CORPUS/cs/` — `src/klist.hpp`/`src/klist.cpp`
> (the ported algorithm: hash-list generation, `hash_merge`, solve — no
> stdio, no logging), `src/cs_gen.cpp` (a separate driver producing
> vectors), `tests/`, `vectors/`.
>
> **Steps, in order**:
> 1. Port `k_list_wagner_algorithm` to C++, omitting
>    `run_with_memory_trace` and all `rich`/`Console`/`Panel` usage —
>    those have no C++ equivalent worth writing. Keep the class's public
>    method shape (`new`, `solve`) recognizable against the Python
>    original.
> 2. Implement `compute_item(i, j)` to produce byte-identical hash input
>    to the Python version: `nonce_bytes + ascii_bytes_of(f"{i}-{j}")` —
>    decimal digits, no leading zeros, no fixed width, literal `-`
>    separator. Do not use `le32`/binary packing here even though that is
>    this project's own convention elsewhere.
> 3. Generate KAT vectors by running the actual Python reference (not a
>    reimplementation) via `k_list_wagner_algorithm.new(n, k, nonce).solve()`
>    at these `(n, K)` points: `(24, 8)`, `(40, 16)` (small, fast,
>    confirms basic correctness) and at least one point each reaching
>    `K` equivalent to Requihash's `k=7` and `k=9` conventions (i.e.
>    `K=128` and `K=512` — confirm the exact `n` needed for `n % (log2(K)+1) == 0`
>    before picking it). Vector schema:
>    `{n, k, nonce_hex, hash_values_and_indices}` — raw solver output,
>    no wire encoding.
> 4. Write `tests/differential.cpp`: for each vectored point, run the C++
>    port with the same `(n, k, nonce)` and assert the returned index
>    vector matches the vector file exactly.
> 5. Write `README.md` in `cs/`: state both conventions explicitly
>    (list-count `k`, ASCII leaf-string encoding) as the first two bullet
>    points, before anything else.
>
> **Stop and report if**: the Python reference fails to run (missing
> `rich`/`tracemalloc` — install them, do not remove the import from the
> *unported* original when running it to generate vectors); a `(n,K)`
> point fails to validate.
>
> **Done means**: the C++ test suite passes for every vectored point
> including the `k=7`/`k=9`-equivalent ones, `README.md`'s first two
> bullets state both conventions, and `src/klist.{hpp,cpp}` has zero
> console/logging/profiling code.
