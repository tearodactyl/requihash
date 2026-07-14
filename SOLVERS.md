# SOLVERS.md — Equihash solver implementations: the original, xenoncat's derivative, and tromp's history

Every date, commit sha, author, and file listed here was pulled directly from
the GitHub API (`api.github.com/repos/tromp/equihash`, `zcash/zcash`,
`zcash/librustzcash`) on 2026-07-13/14, not transcribed from memory or a
secondary source. Where a claim could not be verified this way, it is marked
unverified rather than stated as fact. Findings context: `~/Work/ZK/ZKs/EquihashSurvey.md`
§2–3 (the 2016 optimization-wave narrative this document grounds in commit-level
detail) and `Req/PLAN.md` "A15 detail" (the shorter summary written first, now
superseded in detail by this document, which that entry should point to instead
of duplicate).

Sections 0 (original + xenoncat) and 1–7 (tromp) were researched in separate
sessions from separately cloned repos (`~/Work/ZK/ZKs/equihash-khovratovich`,
`equihash-xenon`, `equihash-tromp`, `BTCGPU-equihash`) — cross-references
between the two halves are noted inline where they connect (e.g. tromp's
README directly narrates his relationship to xenoncat's design). A Node.js
binding lineage (`khovratovich` → `digitalbazaar` → `161chihuahuas/equihash`,
2016–2024) was also researched but is deliberately excluded here as
out-of-scope: this project's own solver work is C++/Rust/assembly, and a
JS-binding modernization has no bearing on it.

## Contents

0. The original implementation and xenoncat's derivative (Khovratovich, xenoncat)
1. Repository facts (tromp)
2. Full commit history, annotated (2016-10-13 → 2018-08-07)
3. What each wave of changes actually did
4. Community contributors, issues, and PRs — including a flagged identity match
5. Integration into zcashd, then librustzcash, then the `equihash` crate
6. The frozen snapshot: what zcashd copied and what it never resynced
7. File inventory: upstream vs. the vendored port

## 0. The original implementation and xenoncat's derivative

### 0.1 `khovratovich/equihash` — the authors' own reference solver

Repo: `github.com/khovratovich/equihash`, created 2016-06-09, last pushed
2016-09-22 — predates tromp's repo (created 2016-10-13) by about a month and
predates the entire Zcash Open Source Miner Challenge optimization wave
entirely. CC0-licensed (public domain, no attribution required). Source is
`Source/C++11/pow.h` + `pow.cc`, 117 + 218 lines — the whole reference
implementation is ~335 lines.

**Interface, verified directly from `pow.h`:**

    class Equihash{
    public:
        Equihash(unsigned n_in, unsigned k_in, Seed s);
        Proof FindProof();
        void FillMemory(uint32_t length);
        void InitializeMemory();
        void ResolveCollisions(bool store);
        std::vector<Input> ResolveTree(Fork fork);
    };

`n` and `k` are plain constructor arguments — no hardcoded parameter set
anywhere in the class. The CLI (`README.md`) confirms: `./equihash -n N -k K
-s Seed`, e.g. `-n 120 -k 5` for a ~32MB instance.

**Recommended parameters, stated in the README, not Zcash's later (200,9):**
cryptocurrency use `(100/110/120,4)`, `(108/114/120/126,5)`; client-puzzle use
`(60/70/80/90,4)`, `(90/96/102,5)`. Zcash's (200,9) was a subsequent,
deliberately harder choice than anything the original authors themselves
suggested — worth noting given `Equihash.md` F-A9's point about parameter
choices never being revisited after 2017.

**Memory layout, verified from `pow.cc`'s `InitializeMemory()`:**

    tupleList = std::vector<std::vector<Tuple>>(tuple_n, def_tuples);

Nested `vector<vector<Tuple>>` — one heap allocation per row, the exact
representation `Req/OPTIMIZATION_HISTORY.md` and `Req/SIZING.md` §2a
independently found dominates a naive Equihash solver's real allocation cost
(measured ~59% of samples via time-profiling, 20–52× the "naive formula"
peak memory via a counting allocator). **This is now directly confirmed**:
Req's own naive baseline is not a strawman built to make later optimizations
look good — it structurally matches the original authors' own 2016 reference
implementation, independently arrived at.

### 0.2 `xenoncat/equihash-xenon` — the index-pointer breakthrough, documented by its own author

Repo: `github.com/xenoncat/equihash-xenon`. Pseudonymous author ("xenoncat").
Hand-written x86 assembly (NASM/FASM syntax, separate AVX1 and AVX2 code
paths for Windows and Linux), not portable C/C++ — a materially different
engineering choice than both the original reference and tromp's later C
solver. `history.log` gives three dated entries: initial single-threaded
AVX2-only version 2016-10-13, AVX1 added 2016-10-17, an ASLR bugfix
2016-10-21 (triggered by a user report, "vattay's OSX ASLR issue").

**A 5-page algorithm-description PDF ships in the repo**
(`notes/algorithm description.pdf`) — a genuine primary source, xenoncat's
own account of the (200,9) solver design, worth recording precisely since it
predates and grounds several things this project has cited secondhand:

- **The binary-tree, no-index-list design.** Each `Pairs` array entry is a
  32-bit reference to two items in the *previous* stage's `Pairs`/`Xorwork`,
  not a growing list of raw indices. Recovering the final 512 indices (at
  k=9) is a backtracking walk: 2 items in `Pairs 8` → 4 in `Pairs 7` → 8 in
  `Pairs 6` → ... → 512 raw indices from `Pairs 1`. This is the literal
  mechanism Req's own README describes as "no index-pointer storage yet" —
  seeing xenoncat's own diagram of it is a direct, dated primary source for
  what `Req/PLAN.md` A6 needs to port.
- **256-bucket coarse sort**, chosen empirically: "Earlier implementations
  experimented with 2048 buckets, 1024 buckets and 512 buckets. Performance
  is best with 256 buckets." A full bit-allocation table is given per stage
  (e.g. State 0: bits `[199:192]` select the bucket, `[191:180]` drive the
  Stage-1 collision search on the remaining 12-bit pattern).
- **Pairs compression to 32 bits.** A pair of item-IDs `(b, s)` within a
  bucket (`b` big, `s` small, unordered) is packed via a triangular-number
  encoding `x = b(b-1)/2 + s`, decoded via `b = round(sqrt(2x+1))`, `s = x -
  b(b-1)/2`. This packs into 26 bits, leaving 6 of the needed 8 bucket-ID
  bits — the remaining 2 bits are recovered from which of 4 memory
  **partitions** the pair was written to (partitioning by source-bucket
  range, both for multithreading and to make this compression scheme work).
- **Exact memory total, stated by the author**: "So Equihash (200,9) needs
  about 15\*11862016 bytes = 178MB." This is the real number behind the
  `CONTEXT_SIZE 178033152` constant in `README.md`'s API section (matches:
  178033152 / 11862016 ≈ 15.006, i.e. 15 units of one `Pairs`-array's worth
  of memory). The PDF documents **two** memory layouts — the one actually
  shipped in the first solver version, and "an alternative layout... more
  straightforward" discovered while writing the document itself, which
  avoids overlap when transitioning between stages. `params.inc`'s
  `STATE*_DEST` constants (verified in the actual assembly source, not just
  the PDF) implement this alternative layout directly.
- **Trivial-solution / duplicate handling**: duplicate detection (e.g. `(A
  B) xor (A C)`, an invalid double-use of item A) is deliberately *not* done
  during the main Stage 1–9 loop ("likely not worth the time"); a cheaper
  partial check runs Stage 4–8 specifically to stop buckets filling with
  known-trivial partial solutions, and full duplicate detection only happens
  once, in `GetSolutions` after Stage 9 completes.

**Verified against the actual assembly, not just the PDF's prose.** Two
files provide ground truth: `Linux/asm/struct_eh.inc` (the `EH` struct
layout: `hashtab`, `bucket0ptr`/`bucket1ptr`, `workingpairs`, `basemap`,
`pairs`, `buf`) and `Linux/asm/params.inc` (`BUCKETS = 256`, `PARTS = 4`,
`ITEMS = 2896`, per-stage `STATE*_BYTES`/`STATE*_OFFSET`/`STATE*_DEST`
constants) — these match the PDF's description exactly, including the
"alternative layout" `STATE*_DEST` offsets. `proc_ehsolver_avx2.asm` (894
lines) is essentially uncommented raw AVX2 (67 semicolons total, mostly
`rdtsc` timing labels, no algorithmic prose) — **the label skeleton itself
is the extractable sequencing**, and it maps 1:1 onto the PDF's stage
model:

    EhSolver: → _LoopBlake2b: (hash generation, fills State 0)
        → _EhStage1: / _EhStage1inner:  (collision search + XOR, repeats through)
        → _EhStage9: / _EhStage9inner:
        → _EhSolverEpilog: / _EhNoSolution:
        → _ProcEhMakeLinksShr4: / _ProcEhMakeLinks: (tree backtracking = PDF §2)
        → _LoopCheckDup: / _LoopCheckDupInner: (final duplicate pass = PDF §7)
        → _LoopBasemap: → _LoopSort1:..._LoopSort4: (canonical ordering)

No algorithmic comments exist in the `.asm` files themselves beyond register/
stage bookkeeping — the PDF is the only prose documentation of *why*, and it
is precise enough to reconstruct the design without reading the assembly at
all. This is worth citing directly rather than through tromp's secondhand
description whenever this project references "the index-pointer technique."

### 0.3 Tromp's own account of his relationship to xenoncat — a primary source, not inferred

`equihash-tromp/README.md` (142 lines, ungrepped by prior SOLVERS.md research
since the file wasn't cloned locally until this session) is tromp's own
first-person narrative, written at submission time, not a later
retrospective:

> My original submission was triggered by seeing how xenoncat's "has much of
> the same ideas as mine" so that making my open sourcing conditional on
> getting sufficient funding for the Cuckoo Cycle Bounty Fund no longer made
> sense.
>
> I noticed that we both use bucket sorting with tree nodes stored as a
> directed acyclic graph. Upon original submission, I wrote: Compared to
> xenoncat, my solver differs in having way more buckets, wasting some
> memory, having simpler pair compression, being multi-threaded, and
> supporting (144,5). And of course in not using any assembly.

**This directly resolves an open item.** `Req/SIZING.md` §5 flags an
unreconciled gap: "tromp's real measured ~144 MB at (200,9) does not match
the paper's published 49 MB for the same nominal parameters (~3× gap)...
remains open." Tromp's own README states the real number and its direct
point of comparison:

> My solver now needs only 144MB compared to xenoncat's 178MB.

Both are **real, author-stated implementation memory figures** for (200,9) —
tromp's 144MB (matching Req's own citation) directly against xenoncat's
178MB (matching the `CONTEXT_SIZE` constant verified in §0.2 above). Neither
is the paper's 49MB Table 3 asymptotic estimate; both are larger, and now we
know *why* to a specific implementation choice (bucket count 2^12 vs 2^8,
memory-waste tradeoffs tromp names explicitly) rather than an unexplained
discrepancy. This is not a new mystery to solve — it is two independently
documented real numbers that were never actually in tension with each
other, only with the paper's separate asymptotic model. `Req/SIZING.md` §5's
"remains open" framing should be narrowed: the 144MB-vs-49MB gap is now
explained (real bucket-sort implementation overhead vs. an O(n·N)
asymptotic bound), even though the specific bucket-sizing arithmetic inside
`equi_miner.c` that produces exactly 144MB (as opposed to some other real
number) is still unexercised by this project.

**Also from the same README, confirming §4 below independently:** "Add
option -p PREFIX to change intial characters of the personalization string
(**not implemented in assembly and CUDA solvers**)." This is tromp's own
statement, from the general usage section (not the commit that added it) —
independent confirmation that the personalization fix from issue #19 (§4.1
below) only ever reached the plain CPU miner, never the assembly/GPU paths,
consistent with commit `191d3b583`'s message "allow command line
personalization for **plain CPU miners**."

**Performance figures tromp states for his own solver** (4GHz i7-4790K /
GTX980, from the same README): single-thread `equi1` 4.9 Sol/s, 8-thread
22.2 Sol/s; GPU (`eqcuda`) 27.2 Sol/s — all at (200,9). At (144,5): 1.0–2.2
Sol/s at 2.6GB memory (a different, much larger memory footprint at the
easier-k parameter set — consistent with `Req/SIZING.md`'s own finding that
memory scales steeply with `n` even as `k` and thus verify cost stay fixed).

### 0.4 What §0 changes about this project's existing claims

- **Req's naive-solver baseline is now doubly grounded**: previously
  justified by profiling Req's own code; now independently confirmed to
  structurally match the original 1ary-source reference implementation's
  own memory layout (`vector<vector<Tuple>>`), not a strawman.
- **The 144MB-vs-49MB reconciliation gap (`Req/SIZING.md` §5, `Req/PLAN.md`
  A12) has a source now**: tromp's own README, stating both his 144MB and
  xenoncat's 178MB directly, as real author-measured (200,9) figures. Worth
  citing there instead of leaving the item as "not started."
  `equi_miner.c`'s exact bucket-sizing arithmetic remains genuinely
  unexercised, so the item isn't fully closed — but it is no longer an
  unexplained number.
- **The index-pointer/tree-backtracking design A6 needs to port now has a
  primary-source specification**: xenoncat's own 5-page PDF, not just
  tromp's later C implementation of the same idea — useful as a second,
  independent description to check a ported implementation against.
- **The `-p` personalization scope limit (assembly/CUDA excluded) is now
  confirmed by two independent statements from tromp** — the commit message
  (`191d3b583`, "for plain CPU miners") and the general README prose ("not
  implemented in assembly and CUDA solvers") — not just one data point.

## 1. Repository facts

- Repo: `github.com/tromp/equihash`, "multi-parameter Equihash proof-of-work
  multi-threaded C solvers." Created 2016-10-13T23:16:40Z. Not archived, still
  public, default branch `master`.
- **143 commits total**, spanning 2016-10-13 to 2018-08-07 — nearly two years
  of real, dated activity, not a one-shot dump.
- Author on effectively every commit: **John Tromp** (`john.tromp@gmail.com`),
  with a handful of merged community PRs (§4).
- The repo's "pushed_at" GitHub metadata shows 2020-10-01, which is a Git
  ref/settings touch, not a new commit — the last actual commit remains
  2018-08-07 (`fab686ed3`).

## 2. Full commit history, annotated

All 143 commits, chronological, condensed to the ones that changed behavior
(merge commits and pure formatting/typo fixes omitted from this table but
counted above; every commit's raw message is in the repo's own history if
needed). Grouped into eight waves that read as one continuous fast-iteration
period, then two years of long-tail parameter/portability additions.

### Wave 0 — bring-up (2016-10-13 to 2016-10-14, day one/two)

| Date | sha | What |
|---|---|---|
| 2016-10-13 | `e22714c5f` | Initial commit |
| 2016-10-14 | `72db6ff8a` | "add solvers" — the first working solver code |
| 2016-10-14 | `b019dd46d` | "get allocation right" |
| 2016-10-14 | `f86b85432` | "add build instructions" |

### Wave 1 — memory and bucket-layout tuning (2016-10-16 to 2016-10-19)

| Date | sha | What |
|---|---|---|
| 2016-10-16 | `786b37d70` | "optimally pre-allocate all memory" |
| 2016-10-17 | `a29c3ac5c` | **"refactor types and reduce buckets from 2^16 to 2^12"** — the first of two bucket-count reductions this repo's `Equihash.md` (§2) already credits as tromp's memory-layout contribution |
| 2016-10-18 | `e088f4c29` | "develop cuda" — CUDA work begins, two days after the CPU solver existed |
| 2016-10-19 | `1cf76fa97` | "save lotsa memory" |
| 2016-10-19 | `32afd65e9` | "manual bitfields" — the packed `tree` struct representation still present in the code today |

### Wave 2 — the pinned snapshot's moment: 2016-10-20

| Date | sha | What |
|---|---|---|
| 2016-10-20 03:03 | **`690fc5eff`** | **"tiny speedups" — the exact commit zcashd's Daira Hopwood copied 90 minutes later** |
| 2016-10-20 04:33 | *(zcashd)* | Hopwood's import, pinning this exact sha (see §5) |
| 2016-10-20 | `f99d9abb1` | "fix dupe bugs" |
| 2016-10-20 | `13c805b17` | "replace qsort by mergesort and xor 64 bits" |

### Wave 3 — xenoncat's BLAKE2b adopted, CUDA hardening (2016-10-21 to 2016-10-26)

| Date | sha | What |
|---|---|---|
| 2016-10-21 | `2d3cd5f94` | "start blake2bp work on dev_miner" |
| 2016-10-22 | `9d5f985b6` | "some more micro-optimizations thx to extensive benching" |
| 2016-10-22 | `63afe5b8c` | "add 144,5 test" — the 144,5 parameter set explicitly tested this early |
| **2016-10-23** | **`fa73e24c4`** | **"try use xenoncat's blake2b"** — tromp directly adopting xenoncat's BLAKE2b work, a full six weeks before the Zcash Open Source Miner Challenge results were announced (2016-12-03) |
| 2016-10-23 | `ef21c9428` | "add other xenoncat blake2b files" |
| 2016-10-23 | `ff116e799`, `74354a1dc` | PR #5 from `rudi-cilibrasi`: "Fix C linkage for C++" |
| 2016-10-23 | `fd85e3b1f`, `6bb262716` | PR #6 from `rudi-cilibrasi`: "Remove binaries from repo and add Makefiles to build as necessary" |
| 2016-10-24 | `22fc059af` | "new headernonce setup and many small changes" |
| 2016-10-24 | `b86a43932` | "add attributions" — attribution to xenoncat's BLAKE2b formally added |
| 2016-10-26 | `882bc1ff7`, `0010bc021` | "fix MAXSOLS bug" (CPU and CUDA) |

### Wave 4 — AVX2, blake2bip, documentation push (2016-10-26 to 2016-10-28)

| Date | sha | What |
|---|---|---|
| 2016-10-26 | `fe024ae0d` | "prepare blake2bip" |
| 2016-10-27 | `6473d85b1`, `7f5063ba7` | "try make blake2bip work" / "something is working" — the AVX2-intrinsics BLAKE2 backend (`blake2-avx2/blake2bip.c` in the current tree) |
| **2016-10-27** | **`fc72754de`** | **"change 2nd stage bucketsort to slot linking"** — an algorithmic change to the merge structure, not just a constant-factor tweak |
| 2016-10-27 | `4c463a869` | "allow AVX2 of course" |
| 2016-10-27 | `d3454d922` | "separate make targets for AVX2" |
| 2016-10-27 | `a2ccd7a28` | "note mem size" — first documented memory figures in the README |
| 2016-10-28 | `bf5c2b01f` | "add -x <hexheader> option; rename blake2b to blake2-asm" |

### Wave 5 — bug fixes from real users mining testnet (2016-11-01 to 2016-11-13)

| Date | sha | Reporter (if external) | What |
|---|---|---|---|
| 2016-11-01 | `946da9652` | — | "fix error reporting bug" |
| 2016-11-03 | `16ee1c8a6` | **elbandi** (issue) | "fix verify bug found by elbandi" |
| 2016-11-09 | `024aff493` | **nicehashdev** (issue #3, "Various versions") | "fix lack of memory clearing bug pointed out by nicehashdev" |
| 2016-11-11 | `13770ee55`, `94736965d` | — | "optimize dupe test" (CPU and CUDA) |
| 2016-11-12 | `525a16a1f` | — | "replace USE_AVX2 by NBLAKES; try x8 as well as x4" |

### Wave 6 — the Cantor-coding breakthrough (2016-11-16 to 2016-11-19)

The wave `~/Work/ZK/ZKs/EquihashSurvey.md` §2 already identifies as the single most
consequential optimization in the whole record — now dated to the day.

| Date | sha | What |
|---|---|---|
| 2016-11-16 | `e8dd46b19`, `db8cf0bea` | "ultimately simplify and speed up duplicate test" / "methodize duplicate test" |
| **2016-11-17** | **`d83965a17`** | **"add cantor slots enabling 2^10 buckets"** |
| **2016-11-17** | **`fec951a2a`** | **"add cantor slots enabling 2^10 buckets to equi_miner"** — the equi_miner-specific application of the same technique |
| 2016-11-17 | `bd8cae6da` | "fix typoes/bugs" |
| 2016-11-18 | `c26df4939` | "unroll 2^10 buckets" |
| 2016-11-19 | `e68142276` | "fix bugs" |
| 2016-11-19 | `bfddb1819` | "phemto optimizations" |
| **2016-11-19** | **`33fed1c9d`** | **"change equi_miner to 2^10 buckets; obsolete dev_miner"** — the moment `equi_miner` (the file later vendored into zcashd/the crate) becomes tromp's primary, most-optimized solver, and the older `dev_miner` is deprecated |
| 2016-11-25 | `0371fb3c9` | "add self assessment" |

### Wave 7 — long-tail parameter and portability support (2017-01-29 to 2018-08-07)

The pace drops sharply after November 2016 — from ~130 commits in five weeks
to 13 commits over the following 21 months, consistent with the record's
"open development on 200,9 effectively stops once ASICs land" reading
(`Equihash.md` §6) but showing tromp kept the repo alive for smaller-parameter
and portability requests specifically.

| Date | sha | What |
|---|---|---|
| 2017-01-29 → 2017-01-31 | `478038659`, `04157c425`, `b235b80d5` | "start add support for DIGITBITS<16" → "small digit support seems to work" |
| 2017-08-02 | `84d814996` | **"make N=96, K=5 work"** |
| 2017-08-08 | `52b71897e` | "generalize eqcuda <n,k> parameters" |
| 2018-05-10 | `7c5f1b220`, `afdaafe58` | PR #16 from **sebastianst**: "write nonce to correct position in header" |
| 2018-05-11 | `f2b7195d0`, `85cc9aa97` | PR #18 from **sebastianst**: "Add command line option -c to print solutions in compressed form" |
| 2018-06-09 | `8d85a6cdb`, `e36ff0521`, `17b76cb3e` | "add 192,7 cuda support" (three-commit fix sequence) |
| **2018-07-10** | **`191d3b583`** | **"allow command line personalization for plain CPU miners"** — see §4, resolved same-day against an external request |
| 2018-08-07 | `fab686ed3` | "fix initialization bug identified by https://github.com/YihaoPeng" — the repo's last commit |

## 3. What each wave of changes actually did

Reading the eight waves as a whole: this is not "one optimization," it is a
**compounding sequence** where each wave's win became the baseline the next
wave optimized further —

1. Get something correct and running (Wave 0).
2. Tune memory layout and bucket count once (Wave 1: 2^16 → 2^12 buckets).
3. **Freeze point** (Wave 2) — this is where zcashd forked off.
4. Adopt a faster hash primitive from a rival implementor (Wave 3: xenoncat's
   BLAKE2b), fix correctness bugs surfaced by real external testers.
5. Add a second, wider SIMD hash backend and an algorithmic merge-structure
   change (Wave 4: AVX2 + slot linking).
6. Fix bugs found by miners running real testnet hardware (Wave 5).
7. **Second bucket-count reduction** (Wave 6: 2^12 → 2^10 via Cantor coding) —
   the optimization this project's `Equihash.md` already singles out as the
   most consequential in the record, now dated precisely to 2016-11-17, 28
   days after the code zcashd still runs was frozen.
8. Long-tail parameter generalization and portability fixes for external
   users, at a much slower cadence, through mid-2018 (Wave 7).

## 4. Community contributors, issues, and PRs

Named external contributors whose work landed in tromp's repo, verified via
the GitHub issues/PRs API:

| Person | Contribution | Date |
|---|---|---|
| **xenoncat** | BLAKE2b implementation adopted wholesale (Wave 3) — not a PR/issue, a direct code adoption with attribution added (`b86a43932`) | 2016-10-23/24 |
| **rudi-cilibrasi** | PR #5 (C linkage fix), PR #6 (remove binaries, add Makefiles) | 2016-10-23 |
| **elbandi** | Reported a verify() bug, fixed same week | 2016-11-03 |
| **nicehashdev** | Issue #3 "Various versions"; reported a memory-clearing bug, fixed same day | 2016-11-09 |
| **sebastianst** | Four issues/PRs: nonce position fix, compressed-solution output option, an out-of-bounds-access question, a (48,5) compile failure report | 2018-05 through 2018 |
| **YihaoPeng** | Issue #20 "Unsafe structure `blake2b_param` initialization" — fixed in the repo's final commit | 2018-08-07 |

**Confirmed by the project owner: this is the same identity.** The owner
works on Zero, Zcash, and other projects under the `tearodactyl` GitHub
account and `tearodactylus@gmail.com` — the same identity that filed
**[issue #19, "Vary blake2b 'personal' bytes from command line"](https://github.com/tromp/equihash/issues/19),
opened 2018-07-10T02:38:26Z, closed the same day**. Tromp committed the
requested feature (`191d3b583`) 5 hours 17 minutes later. This is real,
first-hand prior involvement in exactly the personalization mechanism
Zebro's D3 spine (`context` field, `~/Work/ZK/Zebro/CONSENSUS.md` §2.2) now
depends on — not undiscovered history, but a useful thread to pull on for
context and lessons, below.

### 4.1 The 2018 51% attack wave that motivated issue #19

The issue's own text named three chains — Bitcoin Gold ("Bgold"), Snowgem
("sngem"), Zero Currency ("Zero_") — that had already customized their
BLAKE2b personalization strings in response to a wave of real attacks that
hit Equihash-family and other GPU-mineable chains through 2018:

| Date | Chain | What happened | Cost |
|---|---|---|---|
| 2018-04-04 to 04-09 | **Verge (XVG)** | Timestamp-spoofing ("timejacking") bug in the difficulty retarget let an attacker mine blocks with falsified timestamps, dropping difficulty artificially and letting them dominate the Scrypt algorithm among Verge's five accepted PoW algorithms | ~250,000 XVG (~$1.75M); recurred later the same year despite a patch |
| 2018-05-18 to 05-24 | **Bitcoin Gold (BTG)** | Classic rented-hashrate 51% attack: attacker rented sufficient Equihash(200,9) hashpower (shared across every 200,9 chain) to out-mine the real chain, then executed deep reorgs to double-spend against exchanges | 388,201 BTG (~$17.8–18M); Bittrex delisted BTG afterward |
| 2018-06-02/03 | **ZenCash (now Horizen)** | Same mechanism: attacker built a private chain, double-spent against an exchange, released a 38-block-deep reorg once the private chain was longer | ~23,000 ZEN (~$550,000–600,000) |
| 2020-01-23/24 | **Bitcoin Gold (again)** | A second, much cheaper 51% attack — same rented-hashrate mechanism, up to 16-block reorgs, estimated ~$1,200 in rented hashpower per attack | ~7,167 BTG (~$72,000) — proof the underlying vulnerability (shared hashpower pool) was never actually fixed by the chains that didn't personalize |

**The direct causal chain from attack to defense, in Bitcoin Gold's own
words** ([bitcoingold.org, "Equihash-BTG: Our New PoW Algorithm"](http://www.bitcoingold.org/equihash-btg-our-new-pow-algorithm/)):
the vulnerability was that Equihash(200,9) hashpower was "used
interchangeably by multiple coins" — any of it could be rented (NiceHash
being the standard broker) and redirected at whichever chain was weakest
that day. BTG's fix was two-pronged: new parameters (144,5, raising minimum
memory from ~144 MB to 700 MB/2.5 GB) *and*, separately, **a new
personalization string** ("Equihash-BTG" instead of "ZcashPoW") — their own
stated reasoning: *"BTG will dominate the hashrate on this new PoW
algorithm, which is 'personalized' to BTG, adding a layer of
incompatibility versus other coins."* The parameter change and the
personalization change are separable defenses; personalization alone (no
parameter change) already isolates a chain's hashpower pool, since existing
miner binaries are compiled against a specific personalization constant.
**This is exactly what issue #19 asked Tromp's open solver to support as a
runtime option** rather than requiring a recompile per chain — a practical
tooling gap in the open-source mining ecosystem that a hand-attributed
GitHub handle closed in five hours.

### 4.2 What this means for Zebro's D3 spine — and what to watch for

The era system's `pow.context`/personalization field (CONSENSUS.md §2.2)
already gives Zebro exactly this defense by construction — a fresh,
derived, non-Zcash personalization from genesis, not a retrofit under
attack pressure. Two things worth carrying forward from this history,
concretely:

1. **Personalization is necessary but not sufficient.** BTG's own history
   shows it: the January 2020 repeat attack happened *after* their
   parameter change, on the *new* personalized 144,5 pool — meaning
   personalization defends against being attacked by hashpower rented for
   *other, currently-mineable* chains, but does nothing once meaningful
   hashpower exists that mines your specific chain and personalization.
   Zebro's defense-in-depth needs to come from elsewhere (finality depth,
   checkpoint policy — CONSENSUS.md §4) once real hashrate exists, not from
   personalization alone.
2. **Watch for:** any future GPU-miner-ecosystem support question (the D3
   evidence package's "miner-kernel personalization check," PLAN.md/ZEBRO.md
   C2) should account for the fact that personalization support already
   exists as a *deliberate, requested, shipped* feature in the open-source
   solver lineage — the open question for Zebro is whether closed-source
   production miners (miniZ, gminer, lolMiner) expose the same runtime
   option or require a per-chain build, which is exactly what C2 is meant
   to check.

## 5. Integration into zcashd, then librustzcash, then the `equihash` crate

Full detail (every commit, author, date) already recorded in
`Req/PLAN.md`'s "A15 detail" section from the prior session; summarized here
for completeness, with that document as the source of record for this part:

- **2016-10-20**, Daira Hopwood imports tromp's `690fc5eff` into `zcash/zcash`
  (`ae10ed9c4`), then removes its dependency on tromp's *own* extra BLAKE2b
  implementation 90 minutes later (`c7aaab7aa`) — zcashd swapped BLAKE2b
  backends on the same day it imported the solver, independently of tromp's
  own later BLAKE2b work (Wave 3, which happened three days after).
- **2020, 2022**: two further BLAKE2b rewirings inside zcashd (libsodium →
  `blake2b_simd`; FFI → `cxx`).
- **2024-01-04**: Jack Grigg re-imports the same frozen zcashd copy into
  `zcash/librustzcash` (`45652a21a`), converts it to compile as plain C.
- **2024-01-11**: teor removes the multi-threading entirely
  (`b737d0fe2`, "Remove unused thread support to enable Windows
  compilation") — the single-worker limitation Zebro's `equihash` crate
  dependency carries today traces to this exact, named, dated, reasoned
  decision.
- **2026-05-29**: most recent touch, a dead-variable removal by Danny Willems.

## 6. The frozen snapshot: what zcashd copied and what it never resynced

Verified by diffing the exact pinned sha (`690fc5eff`) against tromp's
current master via the GitHub compare API: **112 commits ahead, 0 behind.**
Everything in Waves 3 through 7 above — xenoncat's BLAKE2b, the AVX2/`blake2bip`
backend, the slot-linking merge change, **the Cantor-coding bucket reduction**,
the (96,5)/(192,7) parameter generalization, and the CLI-personalization
feature (§4's flagged item) — postdates the frozen copy and was never pulled
into zcashd, librustzcash, or the `equihash` crate Zebro depends on.

## 7. File inventory: upstream vs. the vendored port

| File (upstream, current) | Lines | Vendored equivalent (`equihash` crate) | Lines | Note |
|---|---|---|---|---|
| `equi_miner.h` | 1160 | `tromp/equi_miner.c` | 737 | Port, not verbatim (§5); multi-threading stripped 2024-01-11 |
| `equi.h` | 133 | `tromp/equi.h` | 47 | Header guards added, macros → inline functions, attribution comment changed |
| `blake/blake2b.cpp` + 3 headers | — | *(not vendored)* | — | Replaced by a Zcash-authored FFI shim into `blake2b_simd` |
| `blake2-asm/`, `blake2-avx2/` (AVX2 BLAKE2, 12 files) | — | *(not vendored)* | — | Never adopted at all — postdates the freeze (Wave 4) |
| `equi_miner.cu`, `dev_miner.cu`, `blake2b.cu` (CUDA) | 35575 + 34919 + 5450 | *(not vendored)* | — | No CUDA path anywhere in the crate |
| `osx_barrier.h` | 75 | *(present in early librustzcash import, later removed)* | — | Windows-portability casualty alongside the threading removal |
