# SOLVER_CORPUS.md — comparative solver/verifier ports (RK, RZ, RT, CS)

Purpose: build a body of solver and verifier implementations spanning the
historical lineage (original authors → tromp's optimized C solver, both
before and after its 2024 single-core stripping → the paper's own
Sequihash reference) to gain hands-on expertise with each design and to
measure real hardware parameters (memory, timing) against implementations
this project did not write, as a check on `Req/`'s own numbers. Each task
below is scoped for standalone execution with no other context needed
from this repository's other documents. Full commit-level provenance and
history for the tromp/equihash lineage (RZ and RT's sources) is not
repeated here — see `~/Work/ZK/Requihash/SOLVERS.md`, which already
covers it in depth.

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
- **Every port includes a measurement harness on the shared `reqbench`
  discipline** (`Req/BENCH.md`, `Req/SOLVER_CORPUS/reqbench/`) as part of
  its own done-criteria, not an optional extra: repeated-trial timing
  (min/median/MAD, not single samples), git-provenance-stamped output, and
  — where the port measures memory at all — a peak-memory figure
  cross-checked against OS RSS, not trusted from one instrument alone.
  Start from `Req/SOLVER_CORPUS/_template/` (a generic skeleton) rather
  than reimplementing this per port; `rz/src/bin/rz_bench.rs` is the
  filled-in reference example. This exists because RZ's own bench harness
  was built ad hoc, single-sample, with no provenance, in an earlier pass
  — later found and fixed, but a later port starting from the template
  shouldn't repeat that gap.
- **Build/Cargo topology**: there is no workspace under `Req/` — every
  port is a standalone crate, built with plain `cargo` commands from
  inside its own directory (`cd SOLVER_CORPUS/<port> && cargo test`), never
  a sweep from `Req/`'s root. `Req/README.md`'s "Rust/Cargo topology"
  section is the canonical explanation (why no workspace, the exact
  path-dependency shape, expected usage) — read it before adding a new
  port's `Cargo.toml`, since a new port depending on `reqbench` must use
  the same relative-path pattern `rz/` does.

## References

- Biryukov, Khovratovich, "Equihash: Asymmetric Proof-of-Work Based on the
  Generalized Birthday Problem," NDSS 2016 —
  [paper](https://www.internetsociety.org/sites/default/files/blogs-media/equihash-asymmetric-proof-of-work-based-generalized-birthday-problem.pdf),
  [reference implementation](https://github.com/khovratovich/equihash)
  (local clone `~/Work/ZK/ZKs/equihash-khovratovich`).
- `tromp/equihash` — [repo](https://github.com/tromp/equihash) (local
  clone `~/Work/ZK/ZKs/equihash-tromp`, full history) — **RT's source**:
  `equi_miner.h`/`equi_miner.cpp`, the full multi-core original, genuine
  `pthread_barrier_t` round synchronization intact. Distinct from the
  vendored, pinned snapshot inside the `equihash` crate,
  `~/.cargo/registry/.../equihash-0.3.0/tromp/` (frozen at commit
  `690fc5eff` and later single-core-stripped downstream, provenance in
  `~/Work/ZK/Requihash/SOLVERS.md` §5) — **RZ's source**: that vendored
  `equi_miner.c` plus its `tromp.rs` FFI wrapper, the exact single-core
  path that reaches Zebra. Both RZ and RT port tromp's C solver algorithm
  (not `verify.rs`/`minimal.rs`, which is a separate, independent, pure-
  Rust verifier already covered by `Req/rust/src/verify/`'s own
  cross-validated backends and not a port target for either).
- `nicehash/nheqminer`, `cpu_tromp/` — [repo](https://github.com/nicehash/nheqminer)
  (local clone `~/Work/ZK/ZKs/nheqminer`), a second, independent copy of
  tromp's multi-threaded solver, useful as a cross-check that RT's
  pthread design in `~/Work/ZK/ZKs/equihash-tromp` is not a one-off — not
  a second port target, just corroboration.
- `ZcashFoundation/zebra`, `zebra-chain/src/work/equihash.rs` — local
  clone `~/Work/ZK/ZKs/zebra`. Not a port target for any task here —
  confirmed by reading it that it only wraps the pinned `equihash` crate
  (`Solution::check`/`Solution::solve` delegate directly to
  `is_valid_solution`/`tromp::solve_200_9`), with no independent algorithm
  of its own. Referenced because RZ's target (the vendored `equi_miner.c`
  reached via that same crate's `tromp.rs`) is, transitively, the actual
  code Zebra's solve path runs — this reference lets that claim be
  checked directly rather than taken on faith.
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

Port the pinned `equihash` crate's **solver-side C code and its Rust FFI
adjuncts** — `tromp/equi_miner.c` plus the `tromp.rs` wrapper that drives
it — into Rust, included in a validation harness. This is the exact,
single-core-stripped algorithmic path that reaches Zebra: Jack Grigg
re-imported zcashd's frozen copy of tromp's solver into `librustzcash`
on 2024-01-04 (`45652a21a`, converting it to compile as plain C), and
teor removed its multi-threading a week later on 2024-01-11
(`b737d0fe2`, "Remove unused thread support to enable Windows
compilation") — full provenance in `~/Work/ZK/Requihash/SOLVERS.md` §5.
Zebra's own `zebra-chain::work::equihash::Solution` is a thin wrapper
around this crate and contains no independent algorithm of its own
(confirmed by reading `Solution::check`/`Solution::solve` — they delegate
directly to `is_valid_solution` and `tromp::solve_200_9`), so this
vendored C, reached via the crate's `tromp.rs` FFI module, **is** "the
actual algorithmic implementation in Zebra" for the solve path.

Confirmed directly from source, not assumed: `tromp/equi_miner.c`'s own
`worker()` function (line ~703) contains no `pthread_barrier_t` or any
other synchronization primitive — it runs `equi_digit0` →
`equi_digitodd`/`equi_digiteven` per round → `equi_digitK` for a single
`tp->id` with no thread fan-out at all, and the crate's `tromp.rs` (its
own `worker` function, distinct from the C one of the same name) calls
`equi_digit0(eq, 0)` etc. directly with a hardcoded `id=0`, never
spawning threads. This *is* genuinely single-core, not merely
single-threaded-by-default — matching `SOLVERS.md` §7's file-inventory
note ("multi-threading stripped 2024-01-11") exactly. Compare against
RT, which targets the same algorithm family *before* that stripping, in
tromp's own repo, with threading intact.

The crate's `verify.rs`/`minimal.rs` (pure Rust tree-fold verifier, no
tromp C code at all — confirmed by grep, zero references to `tromp`/
`extern`/`unsafe` in either file) are a *separate*, independent algorithm
already covered by `Req/rust/src/verify/`'s own three cross-validated
backends; RZ is not a verifier port and does not target those files.

#### Parameters

Not parametrically general — inherits `equi_miner.c`'s own compile-time
specialization via `#if`/`#elif`/`#error` branches (see RT §Parameters
for the exact mechanism, shared between the two files since `equi_miner.c`
is the frozen-and-stripped descendant of `equi_miner.h`). Confirm the
vendored file's exact supported `(WN, RESTBITS)` set directly — expect it
to be a subset of RT's, since stripping happened after the freeze and may
have narrowed build configurations further; do not assume it is identical
without checking.

#### Validation

Both a live cross-check binary and vectors. The vendored C is buildable
directly from the crate's own `tromp/` directory (`equi_miner.c` + `equi.h`
+ `blake2b.h` + `portable_endian.h`, no external repo dependency) — build
it once via a thin CMake/`cc` wrapper, generate a KAT vector set the same
way `Req/PLAN.md` A14 did, and additionally keep the option to invoke the
built binary directly for differential fuzzing beyond the fixed vectors.
The existing `Req/vectors/zcash_kat_*.json` files are verifier-oracle
vectors (RZ can reuse them for validating that RZ's *own* port of
`is_valid_solution`-equivalent logic isn't accidentally required — it
isn't; RZ is solve-only) but are not solver KATs and do not substitute
for freshly generated solve-side vectors here.

Byte-exact target: the raw index set first; additionally the compressed-
pair wire form if it can be extracted from the vendored C without
modifying it (same caveat as RT step 4 — report as a blocker rather than
patching the pinned source if it can't be exposed cleanly).

**Exit criteria**: Rust port produces byte-identical solutions to the
vendored C binary at every `(WN, RESTBITS)` combination the vendored
`equi_miner.c` actually supports (confirmed, not assumed, per
`#if`/`#elif` branches found); the port's own README states explicitly
that this is the single-core-stripped path that reaches Zebra via
`librustzcash`'s `equihash` crate dependency, with the 2024-01-04 import
/ 2024-01-11 stripping dates cited; included in a validation harness that
can run alongside RT's for a direct single-core-vs-multi-core comparison
on the shared pre-strip ancestor algorithm.

### Original

`~/.cargo/registry/src/index.crates.io-*/equihash-0.3.0/tromp/equi_miner.c`
(737 lines) plus sibling `equi.h`, `blake2b.h`, `portable_endian.h` in the
same directory, and `~/.cargo/registry/src/index.crates.io-*/equihash-0.3.0/src/tromp.rs`
(the Rust FFI wrapper that calls into it) — resolve the `*` glob to the
one matching directory on this machine.

### RZ Prompt

> You are porting vendored C solver code, reached through its Rust FFI
> wrapper, into native Rust. This is the exact code path Zebra's
> dependency (`librustzcash`'s `equihash` crate) actually runs to solve —
> not the crate's separate, independent Rust verifier (`verify.rs`/
> `minimal.rs`), which is out of scope here.
>
> **Source**:
> `~/.cargo/registry/src/index.crates.io-*/equihash-0.3.0/tromp/equi_miner.c`
> (737 lines) and its sibling `equi.h` in the same directory, plus
> `~/.cargo/registry/src/index.crates.io-*/equihash-0.3.0/src/tromp.rs`
> (the FFI wrapper — read its `worker` function and note it calls
> `equi_digit0(eq, 0)` etc. with a hardcoded `id=0`, never spawning
> threads). Read all three before writing any Rust. Confirm for yourself
> that `equi_miner.c`'s own `worker()` (around line 703) has no
> `pthread_barrier_t`/synchronization of any kind — this is what makes it
> genuinely single-core, not just single-threaded-by-default.
>
> **Target layout**: new directory
> `~/Work/ZK/Requihash/SOLVER_CORPUS/rz/` — `src/lib.rs` (the ported
> bucket/collision-round algorithm as a library, no I/O), `src/bin/
> rz_gen.rs` (drives the vendored C via FFI or subprocess to build
> vectors — state which choice you made), `tests/`, `vectors/`.
>
> **Steps, in order**:
> 1. Find every `#if`/`#elif`/`#error` branch in `equi_miner.c`'s
>    `getxhash0`/`getxhash1` and bucket-ID functions. List the exact
>    `(WN, RESTBITS)` pairs that compile — do not assume this matches
>    RT's list from `equi_miner.h`; the vendored file may support fewer.
> 2. Port the algorithm to Rust, single-core (matching the source
>    exactly — do not add threading here, that is RT's job on the
>    pre-strip ancestor).
> 3. Build the vendored C as a cross-check binary (thin `build.rs` or
>    CMake wrapper compiling `equi_miner.c` directly — do not modify the
>    vendored source).
> 4. Generate vectors at every `(WN, RESTBITS)` pair from step 1: for
>    each, run the C binary at 3 distinct nonces, capture the index set
>    and, if exposable without modifying the C, the compressed-pair
>    intermediate encoding.
> 5. Write `tests/cross_check.rs`: assert the Rust port's output matches
>    the C binary's output at every vectored point.
>
> **Stop and report if**: the `#if` branches found in step 1 leave the
> vendored file supporting a materially different `(WN, RESTBITS)` set
> than RT's `equi_miner.h` findings (report both lists, don't force them
> to match); extracting the compressed-pair encoding requires modifying
> the vendored C source (report as a blocker, don't modify it).
>
> **Done means**: `cargo test` passes in `rz/` for every `(WN, RESTBITS)`
> pair found in step 1; `README.md` states this targets the vendored,
> single-core-stripped solver path that reaches Zebra (with the
> 2024-01-04/2024-01-11 dates and commit hashes cited), not the crate's
> separate verifier; the report states whether the compressed-pair
> encoding was validated or only the index set was.

---

## RT

### Task

Port tromp's **full, multi-core original** — `equi_miner.h` and its
adjuncts, from tromp's own repository, *before* the 2024-01-11 stripping
that produced the vendored snapshot RZ targets — into Rust, preserving
genuine multi-core execution in some idiomatic Rust approximation (a
`std::sync::Barrier`-based per-round handoff mirroring the original's
`pthread_barrier_t` design, or a `rayon`/scoped-thread redesign — either
is acceptable as long as the round-synchronous structure survives and the
choice is stated explicitly), and carry the result forward for further
review, benchmarking, sizing, and optimization. xenoncat's own work
(`~/Work/ZK/ZKs`, see `SOLVERS.md` §0.2-0.3) may be read for inspiration
— tromp adopted xenoncat's BLAKE2b directly (`SOLVERS.md` Wave 3,
`b86a43932`) and independently arrived at a related but distinct
bucket/index-pointer design — but is **not** an RT implementation
reference; RT ports tromp's actual code, not xenoncat's.

Confirmed directly from source, not assumed: `equi_miner.h`'s `worker()`
(around line 1107) genuinely fans out across a `pthread_barrier_t`
(`eq->barry`, initialized in the `equi` constructor at line ~361 with
`pthread_barrier_init(&barry, NULL, nthreads)`) with ten explicit
`barrier(&eq->barry)` calls gating the per-round handoff between
`equi_digit0`/`equi_digitodd`/`equi_digiteven`/`equi_digitK`. The driver
`equi_miner.cpp` (`main`, reading `argc`/`argv`) takes a real `-t
<nthreads>` flag, `calloc`s one `thread_ctx` per thread, and does genuine
`pthread_create`/`pthread_join` fan-out (lines 92-122) — this is a real,
deployed concurrency design, not a vestigial one. RT should preserve this
property, not merely acknowledge it.

Compare against RZ, which targets the same algorithm family *after*
Grigg's 2024-01-04 librustzcash import and teor's 2024-01-11
multi-threading removal — the exact single-core path that reaches Zebra.
RT and RZ share a common pre-strip ancestor and are complementary: RT
preserves what teor's commit removed, RZ ports what's actually shipped.
Running both against the same `(WN, RESTBITS)` points is directly useful
as a single-core-vs-multi-core comparison on what is otherwise the same
bucket algorithm.

Keep the ported algorithm (the bucket/collision-round logic, plus its
concurrency structure) in its own module with no CLI/console dependency,
same as RK — the `-t`/`-h`/`-n`/`-r`/`-s`/`-x`/`-c` argument parsing in
`equi_miner.cpp`'s `main` is harness code, not algorithm, and stays out of
the ported library.

#### Reference-binary build (decision)

RT's reference binaries build **natively, as straight portable C++**,
against the repository's vendored modern BLAKE2b
(`BLAKE/vendor/blake2`): map the sources' `blake/blake2.h` include to
the vendored header — an include-directory shim when running tromp's
sources unmodified for evaluation; the port itself forks and codes
directly to the vendored header, as `rk/original` does — and link
`blake2b-ref.c`. No source edits, no architecture flags. Verified at
`(48,5)`: native arm64 binary, solve trace identical to the upstream
x86 build, thread-count-invariant at `-t 1/2/4` — tromp's compacted
`blake2b_state` is an internal optimization producing standard BLAKE2b
digests, so the standard layout drops in via the header alone. The
x86-only `blake2-avx2`/`blake2-asm` batch kernels are not needed for
reference purposes (the batching story: `BLAKE/BLAKE.md` §5.3).
Decision record: `BLAKE/BLAKE.md` §0.

#### Parameters

Tromp's C solver is **not parametrically general** — it is specialized
per `(WN, BUCKBITS, RESTBITS)` combination via compile-time
`#if`/`#elif`/`#error` branches in `getxhash0`/`getxhash1` and the two
bucket-ID computation functions. Confirm the exact `(WN, RESTBITS)` set
`equi_miner.h` supports directly from source — do not assume it matches
the vendored `equi_miner.c`'s set (RZ's target); the two files diverged
after the freeze (`SOLVERS.md` §6: "112 commits ahead, 0 behind" — Cantor
bucket coding and other post-freeze changes landed in tromp's repo and
were never pulled into the vendored copy), so `equi_miner.h` may support
additional `RESTBITS` values or a different bucket-addressing scheme the
vendored snapshot lacks. This is fundamentally different from RK's
original, which is parameter-generic at runtime.

The Rust port must pick one of two options explicitly, not blur them:
(a) **mirror the same compile-time specialization** (Rust `const`
generics or a macro expanding the same fixed `(n, restbits)` set) —
faithful to the original's actual design, equally limited; or
(b) **generalize the bit-extraction logic** to work for arbitrary `(n,
restbits)` — a genuine *improvement*, and must be labeled as an
improvement, not presented as "the same algorithm," if done. Option (a)
is the lower-risk starting choice — it validates the port against a
known-fixed parameter set before any generalization risk is introduced;
(b) can be a follow-on once (a) is proven, at which point it also
naturally reaches `k=7`/`k=9` (Zero/Zcash values) if the generalized
bit-extraction is correct for those `RESTBITS`.

#### Validation

Running the original source directly is viable and preferred over
vectors alone — tromp's repo is a stable local clone
(`~/Work/ZK/ZKs/equihash-tromp/`), buildable via its own `Makefile`. Build
`equi_miner.cpp` (which includes `equi_miner.h`) as a cross-check binary
at multiple `-t <nthreads>` values, generate vectors the same way A14
did, kept in their own directory separate from the port's algorithm
module — *and* keep the option to invoke the C binary directly for a
larger differential fuzz pass (many random nonces, multiple thread
counts) beyond what a fixed vector set covers.

Multi-threaded output must additionally be validated as **deterministic**
regardless of thread count — run the reference binary at `-t 1`, `-t 2`,
and `-t 4` (or however many the test machine's core count allows) on the
same input/nonce and confirm identical solution sets before treating any
single run as ground truth; the Rust port must pass the same
thread-count-invariance check, not just single-run equality against one
reference invocation. This is an extra property beyond RZ's validation,
which has no thread-count axis to vary.

Byte-exact target: the compressed-pair wire encoding, not just the index
set. Tromp's triangular-number packing (`x = b(b-1)/2 + s`, mirrored in
`tree_from_bid`) is closer to what `Req/`'s own future `solve::pointer`
production backend (A6) will need to be validated against — RT is the
more directly useful of the two ports for A6, not just an
expertise-building exercise. State this dependency explicitly in RT's own
docs.

**Exit criteria**: Rust port produces byte-identical solutions to the
`equi_miner.h` reference at every `(WN, RESTBITS)` combination it
supports (confirmed, not assumed, per `#if`/`#elif` branches actually
found); the compile-time-specialization-vs-generalization choice is
stated explicitly, not left ambiguous; the compressed-pair encoding is
validated against the original's own encoding, not merely the index set;
the port's concurrency structure is genuine (produces identical solutions
across at least two different thread counts, not merely compiling with a
`-t` flag that is silently ignored); the Rust threading approach chosen
(`std::sync::Barrier` vs. `rayon`/scoped threads vs. other) is stated
explicitly with reasoning; xenoncat's design is cited only as background
context, never as a source ported from.

### Original

`~/Work/ZK/ZKs/equihash-tromp/equi_miner.h` (1160 lines) plus
`equi_miner.cpp` (146 lines, the CLI driver with real `pthread_create`
fan-out), `equi.h`, and `osx_barrier.h` in the same directory. (A second,
independent copy exists at
`~/Work/ZK/ZKs/nheqminer/cpu_tromp/equi_miner.h`, 629 lines, useful only
as a cross-check that the pthread design isn't a one-off in the primary
source — not a second port target.) Explicitly **not** the reduced,
single-core-stripped vendored snapshot inside the `equihash` crate — that
is RZ's target, ported separately.

### RT Prompt

> You are porting tromp's full multi-core C solver to Rust, preserving
> its genuine multi-threaded round-synchronization structure. A second,
> smaller, single-core-stripped source exists (the vendored snapshot
> inside the `equihash` crate, `.../equihash-0.3.0/tromp/equi_miner.c`)
> — that is a *different* task (RZ), targeting the exact code path that
> reaches Zebra after multi-threading was removed downstream in 2024. Do
> not use it as your source here; you are porting the pre-strip, still-
> threaded original.
>
> **Source**: `~/Work/ZK/ZKs/equihash-tromp/equi_miner.h` (1160 lines) and
> `equi_miner.cpp` (146 lines, the CLI driver — read `main`'s
> `pthread_create`/`pthread_join` fan-out at lines ~92-122 to see how
> `-t <nthreads>` actually drives concurrency), plus sibling `equi.h` and
> `osx_barrier.h`. Read all of them before writing any Rust. Locate
> `worker()` (around line 1107) and its ten `barrier(&eq->barry)` calls —
> this is the per-round handoff your Rust port must preserve in some form.
>
> **Steps, in order**:
> 1. Find every `#if`/`#elif`/`#error` branch in `getxhash0`, `getxhash1`,
>    and the two bucket-ID computation functions. List the exact
>    `(WN, RESTBITS)` pairs that compile — confirm against the actual
>    source, do not assume it matches the vendored `equi_miner.c`'s set
>    (it may differ; the two diverged post-freeze per `SOLVERS.md` §6).
> 2. Port the algorithm choosing **option (a)**: mirror the same
>    compile-time specialization using Rust `const` generics or a macro
>    over the same fixed `(WN, RESTBITS)` set found in step 1. Do not
>    generalize to arbitrary parameters in this pass. State this choice as
>    the first line of `rt/README.md`.
> 3. Port the concurrency structure: map `equi_miner.h`'s per-round
>    `pthread_barrier_t` handoff onto a Rust equivalent
>    (`std::sync::Barrier` across scoped threads is the most direct
>    mapping; a `rayon`-based redesign is acceptable if you state
>    explicitly that it's a redesign, not a transliteration). State which
>    approach you chose and why in `rt/README.md`.
> 4. Build `equi_miner.cpp`/`equi_miner.h` as a cross-check binary via the
>    repo's own `Makefile` or a thin CMake wrapper — do not modify the C
>    source.
> 5. Generate vectors at every `(WN, RESTBITS)` pair from step 1: for
>    each, run the C binary at 3 distinct nonces **and** at least two
>    different `-t` thread counts, capture the full solution (index set)
>    **and** the compressed-pair intermediate encoding (`tree.bid_s0_s1`'s
>    packed form, `x = b(b-1)/2 + s`) if the C source exposes it directly
>    — if it does not expose the intermediate encoding without modifying
>    the C, say so explicitly in the report rather than modifying the
>    source to extract it. Confirm the reference binary itself produces
>    identical solution sets across the different thread counts before
>    trusting any single run as ground truth.
> 6. Write `tests/cross_check.rs`: assert the Rust port's output matches
>    the C binary's output at every vectored point, for both the index set
>    and (if captured) the compressed-pair encoding; assert the Rust
>    port's own output is identical across at least two different
>    internal thread/worker counts.
>
> **Stop and report if**: the `#if` branches found in step 1 leave
> `equi_miner.h` supporting a materially different `(WN, RESTBITS)` set
> than what's found for the vendored `equi_miner.c` (report both lists,
> don't force them to match); extracting the compressed-pair encoding
> requires modifying the C source (report as a blocker, don't modify it
> to work around it); the reference C binary itself produces different
> solutions at different thread counts (report as a correctness issue in
> the reference, don't silently pick one thread count as ground truth).
>
> **Done means**: `cargo test` passes in `rt/` for every `(WN, RESTBITS)`
> pair found in step 1, including a thread-count-invariance check;
> `README.md` states the option-(a) choice and the concurrency-mapping
> choice explicitly, and notes xenoncat's work was consulted only as
> background (cite `SOLVERS.md` §0.2-0.3), not ported from; the report
> states explicitly whether the compressed-pair encoding was validated or
> only the index set was.

---

## CS

**Status (2026-07-17): C++ port done; Rust re-port `cs-rs/` also done.**
`SOLVER_CORPUS/cs/` is the canonical C++ port; `SOLVER_CORPUS/cs-rs/` is
a faithful Rust re-port (mirrors RZ's crate layout, plain unkeyed
BLAKE2b via `blake2b_simd`, both conventions + the `big_shr` direction
reproduced), validated byte-exact against the same 4 vectors — the C++
port is its differential oracle. Gap: `cs-rs` has correctness tests but
no `reqbench` bench binary yet (the RZ-style A21 gap). The task spec
below is retained as the original C++ port's brief.

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
