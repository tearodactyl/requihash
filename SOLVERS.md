# SOLVERS.md — Equihash solver implementations

Primary-source history of the three solver lineages this project draws on
— Khovratovich's original reference, xenoncat's index-pointer design, and
tromp's C solver and its path into zcashd/librustzcash/Zebra — kept to
what still matters for `Req/`'s own work today: what each design actually
did, what's provably true about it, and where the resulting code lives
now. Commit-level and session-level detail that doesn't feed a current
claim has been cut; see git history for anything not repeated here.

Local clones: `~/Work/ZK/ZKs/equihash-khovratovich`, `equihash-xenon`,
`equihash-tromp`, `BTCGPU-equihash`. A Node.js binding lineage
(`khovratovich` → `digitalbazaar` → `161chihuahuas/equihash`, 2016-2024)
exists but is out of scope — this project's own solver work is
C++/Rust/assembly, not JS.

## Contents

0. The original reference and xenoncat's derivative
1. Tromp's solver: origin, evolution, and GPU support
2. Community contributions and the 2018 personalization story
3. Integration into zcashd, librustzcash, and the `equihash` crate
4. The frozen snapshot: what was never resynced
5. File inventory: upstream vs. the vendored port

## 0. The original reference and xenoncat's derivative

### 0.1 Khovratovich's reference solver

Repo: `khovratovich/equihash`. CC0-licensed. Source is `Source/C++11/pow.h`
+ `pow.cc`, 117 + 218 lines total.

Interface, verified directly from `pow.h`:

    class Equihash{
    public:
        Equihash(unsigned n_in, unsigned k_in, Seed s);
        Proof FindProof();
        void FillMemory(uint32_t length);
        void InitializeMemory();
        void ResolveCollisions(bool store);
        std::vector<Input> ResolveTree(Fork fork);
    };

`n` and `k` are plain constructor arguments with no hardcoded parameter
set — this is a genuinely parameter-generic reference, broader in range
than tromp's later compile-time-specialized solver. The README's own
recommended parameters (`(100/110/120,4)`, `(108/114/120/126,5)` for
cryptocurrency use) predate and are smaller than Zcash's eventual (200,9).

Memory layout, from `InitializeMemory()`:

    tupleList = std::vector<std::vector<Tuple>>(tuple_n, def_tuples);

A nested `vector<vector<Tuple>>` — one heap allocation per row. This is
the same representation `Req/ARCHITECTURE.md` §7 and `Req/SIZING.md` §2a
independently found dominates a naive solver's real allocation cost
(~59% of samples by time-profiling; 20-52× the "naive formula" peak
memory by counting allocator). Req's own naive-solver baseline therefore
structurally matches the original authors' own reference design, not a
strawman built to make later optimizations look good.

### 0.2 xenoncat's index-pointer solver

Repo: `xenoncat/equihash-xenon`, pseudonymous author. Hand-written x86
assembly (NASM/FASM, separate AVX1/AVX2 paths), not portable C/C++.

The repo ships a 5-page algorithm-description PDF
(`notes/algorithm description.pdf`) — a primary source for the design
that broke Equihash's ASIC resistance, worth having on file precisely
rather than only through tromp's later, secondhand C implementation:

- **Binary-tree, no-index-list design.** Each `Pairs` array entry is a
  32-bit reference to two items in the *previous* stage, not a growing
  list of raw indices. Recovering the final 512 indices (k=9) is a
  backtracking walk through 9 stages. This is the mechanism `Req/PLAN.md`
  A6 (compact index-pointer storage) needs to port.
- **256-bucket coarse sort**, chosen empirically over 2048/1024/512-bucket
  alternatives that performed worse.
- **Pairs compression to 32 bits**: a pair of item-IDs `(b, s)` within a
  bucket packs via triangular-number encoding `x = b(b-1)/2 + s`, decoded
  via `b = round(sqrt(2x+1))`, `s = x - b(b-1)/2`.
- **Real memory total, stated by the author**: "Equihash (200,9) needs
  about 15*11862016 bytes = 178MB" — the number behind the
  `CONTEXT_SIZE 178033152` constant in the repo's own README.
- **Duplicate handling**: full duplicate detection happens once, after
  all stages complete, not during the main loop — a cheaper partial check
  during stages 4-8 prevents buckets from filling with known-trivial
  partial solutions.

Verified against the assembly itself, not just the PDF's prose:
`Linux/asm/struct_eh.inc` and `params.inc` (`BUCKETS = 256`, `PARTS = 4`,
`ITEMS = 2896`) match the PDF's description exactly, including an
alternative memory layout the PDF documents as a later improvement over
what shipped first. The directory as a whole — a thin top-level
`equihash_avx1.asm`/`equihash_avx2.asm` build unit `include`-ing
`proc_ehprepare_avx*.asm`/`proc_ehsolver_avx*.asm` — is what reconstructs
the design; no single file stands alone as "the" solver.

Xenoncat published no GPU code: the repo has no `.cu`/CUDA/OpenCL files,
and the algorithm-description PDF describes multithreading (CPU partition
assignment) but never GPU. Xenoncat's confirmed acceleration work is CPU
SIMD assembly only.

## 1. Tromp's solver: origin, evolution, and GPU support

Repo: `tromp/equihash`, created 2016-10-13, 143 commits through
2018-08-07, effectively all authored by John Tromp.

Tromp's own README (written at submission time, not a later retrospective)
states his design's relationship to xenoncat's directly:

> My original submission was triggered by seeing how xenoncat's "has much
> of the same ideas as mine"... Compared to xenoncat, my solver differs in
> having way more buckets, wasting some memory, having simpler pair
> compression, being multi-threaded, and supporting (144,5). And of course
> in not using any assembly.
>
> My solver now needs only 144MB compared to xenoncat's 178MB.

Both figures — tromp's 144MB and xenoncat's 178MB (matching the
`CONTEXT_SIZE` constant confirmed in §0.2) — are real, author-stated
implementation memory footprints for (200,9), not the paper's separate
49MB asymptotic Table 3 estimate (`Req/SIZING.md`). The gap between real
implementations and the asymptotic bound is explained by bucket-sort
overhead (tromp: 2^12 buckets vs. xenoncat's design's own tradeoffs), not
an unresolved discrepancy.

**What changed, and what still matters today.** Tromp's own solver went
through the same broad progression xenoncat's did — initial bring-up,
memory/bucket-layout tuning, adoption of xenoncat's BLAKE2b work
(2016-10-23, roughly six weeks before the Zcash Open Source Miner
Challenge results were announced), an AVX2 SIMD backend, and a second
bucket-count reduction via Cantor coding (2^12 → 2^10, 2016-11-17) that
this project's `Equihash.md` already identifies as the single most
consequential optimization in the historical record. One specific commit,
`690fc5eff` (2016-10-20), is the exact snapshot zcashd forked 90 minutes
later — everything after it (the BLAKE2b adoption, the Cantor reduction,
later parameter generalization) never reached the vendored copy Zebra
depends on today (§4). Development slowed sharply after November 2016 —
consistent with real optimization effort stopping once ASICs made further
CPU/GPU solver work moot — but the repo stayed alive for smaller-parameter
and portability requests through mid-2018.

Tromp's own stated performance figures (4GHz i7-4790K / GTX 980, at
(200,9)): single-thread 4.9 Sol/s, 8-thread 22.2 Sol/s, GPU 27.2 Sol/s.

### Tromp's CUDA support

Real, complete CUDA kernels exist: `equi_miner.cu`/`dev_miner.cu`/
`blake2b.cu`, one `__global__` kernel per Wagner round (`digitH`,
`digit_1` through `digit8`, generic `digitO`/`digitE`/`digitK`), standard
`blockIdx.x * blockDim.x + threadIdx.x` indexing, `cudaMalloc`/
`cudaMemcpy` state transfer. Build target is `nvcc -arch sm_35` (CUDA
compute capability 3.5, the Kepler generation, ~2012-2014) — no later
`-arch` target appears anywhere in the repo's history. The CUDA path was
actively maintained past its initial (200,9)-only form: it was later
generalized to arbitrary `(n,k)` and extended to support Zero Currency's
(192,7) parameter set. GPU throughput (27.2 Sol/s) beats the 8-thread CPU
figure (22.2 Sol/s) on the same era of hardware, but not by the
order-of-magnitude gap GPU mining shows on less memory-bound workloads —
consistent with Equihash's design intent of being memory-bandwidth-bound,
which caps the GPU's structural advantage. None of this CUDA code was
ever vendored into zcashd or the `equihash` crate (§5).

## 2. Community contributions and the 2018 personalization story

External contributors whose work landed in tromp's repo: **xenoncat**
(BLAKE2b, adopted wholesale with attribution, 2016-10-23/24);
**rudi-cilibrasi** (C linkage fix, build-system cleanup); **elbandi** and
**nicehashdev** (bug reports from real testnet mining, fixed same
day/week); **sebastianst** (nonce-position fix, compressed-solution output
option, 2018); **YihaoPeng** (an unsafe-initialization bug, fixed in the
repo's final commit).

One contribution is directly relevant to this project's own work: the
same GitHub identity behind this project (`tearodactyl`) filed
[issue #19](https://github.com/tromp/equihash/issues/19), "Vary blake2b
'personal' bytes from command line," on 2018-07-10 — tromp shipped the
requested feature (commit `191d3b583`) five hours later. Two independent
statements from tromp confirm the fix was scoped to the plain CPU miner
only, never the assembly or CUDA paths.

**Why that request existed: the 2018 Equihash 51%-attack wave.** The
issue itself named three chains (Bitcoin Gold, Snowgem, Zero Currency)
that had already customized their BLAKE2b personalization in response to
real attacks:

| Date | Chain | Mechanism | Cost |
|---|---|---|---|
| 2018-05 | Bitcoin Gold | Rented Equihash(200,9) hashpower — shared across every 200,9 chain — out-mined the real chain for a deep reorg | ~388,201 BTG (~$18M) |
| 2018-06 | ZenCash | Same mechanism, a 38-block-deep reorg | ~23,000 ZEN (~$550-600K) |
| 2020-01 | Bitcoin Gold (again) | A second, much cheaper repeat — ~$1,200 in rented hashpower per attack | ~7,167 BTG (~$72K) |

Bitcoin Gold's own stated fix was two-pronged: new parameters (144,5,
raising minimum memory from ~144MB to 700MB-2.5GB) *and*, separately, a
new personalization string — their own reasoning being that a chain-
specific personalization "adds a layer of incompatibility versus other
coins," isolating its hashpower pool from rentable Equihash(200,9)
capacity regardless of parameter choice. Personalization alone (no
parameter change) already achieves this isolation, since existing miner
binaries are compiled against a specific personalization constant — which
is exactly what issue #19 asked tromp's open solver to support as a
runtime option rather than a per-chain recompile.

**Two lessons that carry forward directly to this project's own
personalization design** (`Req/SPEC.md` §2's `context` field):
personalization is necessary but not sufficient — Bitcoin Gold's 2020
repeat attack happened *after* their parameter and personalization
change, once real hashpower existed that specifically targeted their new
pool, meaning defense-in-depth ultimately has to come from elsewhere
(finality depth, checkpoint policy) once meaningful hashrate exists.
Personalization support in open-source solvers is a real, shipped,
demand-driven feature, not a hypothetical gap — the open question for any
production deployment is whether closed-source miners expose the same
runtime option or require a per-chain build.

## 3. Integration into zcashd, librustzcash, and the `equihash` crate

- **2016-10-20**: Daira Hopwood imports tromp's `690fc5eff` into
  `zcash/zcash`, then removes its dependency on tromp's own BLAKE2b
  implementation 90 minutes later — zcashd swapped BLAKE2b backends the
  same day it imported the solver, independently of tromp's own later
  BLAKE2b work (which happened three days after).
- **2020, 2022**: two further BLAKE2b rewirings inside zcashd (libsodium
  → `blake2b_simd`; FFI → `cxx`).
- **2024-01-04**: Jack Grigg re-imports the same frozen zcashd copy into
  `zcash/librustzcash`, converting it to compile as plain C.
- **2024-01-11**: teor removes the multi-threading entirely ("Remove
  unused thread support to enable Windows compilation") — the
  single-worker limitation the pinned `equihash` crate carries today, and
  the exact single-core-stripped path RZ (`Req/SOLVER_CORPUS.md`) ports.

## 4. The frozen snapshot: what was never resynced

Verified by diffing the pinned sha (`690fc5eff`) against tromp's current
master: **112 commits ahead, 0 behind.** Everything after the freeze —
xenoncat's BLAKE2b, the AVX2 backend, the Cantor-coding bucket reduction,
later parameter generalization, and the CLI-personalization feature (§2)
— postdates the frozen copy and was never pulled into zcashd,
librustzcash, or the `equihash` crate Zebra depends on. RT
(`Req/SOLVER_CORPUS.md`) targets the full, current upstream repo
specifically because of this gap — it is the only one of the two lineages
with the post-freeze improvements, including genuine multi-threading,
intact.

## 5. File inventory: upstream vs. the vendored port

| File (upstream, current) | Lines | Vendored equivalent (`equihash` crate) | Lines | Note |
|---|---|---|---|---|
| `equi_miner.h` | 1160 | `tromp/equi_miner.c` | 737 | Port, not verbatim; multi-threading stripped 2024-01-11 |
| `equi.h` | 133 | `tromp/equi.h` | 47 | Header guards added, macros → inline functions |
| `blake/blake2b.cpp` + 3 headers | — | *(not vendored)* | — | Replaced by a Zcash-authored FFI shim into `blake2b_simd` |
| `blake2-asm/`, `blake2-avx2/` (12 files) | — | *(not vendored)* | — | Postdates the freeze |
| `equi_miner.cu`, `dev_miner.cu`, `blake2b.cu` | 35575 + 34919 + 5450 | *(not vendored)* | — | No CUDA path anywhere in the crate |
| `osx_barrier.h` | 75 | *(present in early librustzcash import, later removed)* | — | Windows-portability casualty alongside the threading removal |
