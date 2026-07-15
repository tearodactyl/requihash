# RZ port — status

Scope for this pass: **(WN=144, WK=5, RESTBITS=4) only.** (200,8) and
(200,9) are explicitly out of scope for this pass (see
`Req/SOLVER_CORPUS.md` §RZ prompt in this session — deliberately scoped
down after a prior attempt spread effort across three ports with nothing
finished). Do not attempt them here.

## Step 1: algorithm understanding (checkpoint before writing Rust)

Source read in full:
- `~/.cargo/registry/src/index.crates.io-*/equihash-0.3.0/tromp/equi_miner.c`
  (737 lines — despite the `.c` extension this is include-guarded as
  `ZCASH_POW_TROMP_EQUI_MINER_H`, i.e. it's the frozen, stripped
  descendant of upstream tromp's `equi_miner.h`, confirming the
  `SOLVER_CORPUS.md` §RZ provenance note).
- `~/.cargo/registry/src/index.crates.io-*/equihash-0.3.0/tromp/equi.h`
  (48 lines).
- `~/.cargo/registry/src/index.crates.io-*/equihash-0.3.0/src/tromp.rs`
  (the FFI wrapper — its own `worker()`, not the C file's `worker()`,
  drives the single-core, id=0, no-thread-spawn sequence).
- `~/.cargo/registry/src/index.crates.io-*/equihash-0.3.0/src/params.rs`,
  `src/blake2b.rs`, `src/verify.rs::initialise_state` (base BLAKE2b state
  construction).

Already-working harness (`cross_check_c/harness_main.c`,
`blake2b_glue.c/h`, `build.rs`) independently reproduces the base-state
construction and the exact driver sequence — cross-referenced against
`tromp.rs::worker` and confirmed identical. Not redoing that work; just
noting it as the second confirmation source.

### Compile-time constants at (WN=144, WK=5, RESTBITS=4)

Derived from `equi.h` + `equi_miner.c` macro definitions:

| symbol | formula | value |
|---|---|---|
| NDIGITS | WK+1 | 6 |
| DIGITBITS | WN/NDIGITS | 24 |
| PROOFSIZE | 1<<WK | 32 |
| BASE | 1<<DIGITBITS | 16777216 |
| NHASHES | 2*BASE | 33554432 |
| HASHESPERBLAKE | 512/WN | 3 |
| HASHOUT | HASHESPERBLAKE*WN/8 | 54 |
| BUCKBITS | DIGITBITS-RESTBITS | 20 |
| NBUCKETS | 1<<BUCKBITS | 1048576 |
| SLOTBITS | RESTBITS+1+1 | 6 |
| SLOTRANGE | 1<<SLOTBITS | 64 |
| SAVEMEM | (RESTBITS==4 branch) | 1 |
| NSLOTS | SLOTRANGE*SAVEMEM | 64 |
| XFULL | (fixed) | 16 |
| NRESTS | 1<<RESTBITS | 16 |
| NBLOCKS | ceil(NHASHES/HASHESPERBLAKE) | 11184811 |
| HASHWORDS0 | WORDS(WN-DIGITBITS+RESTBITS) = WORDS(124) | 4 |
| HASHWORDS1 | WORDS(WN-2*DIGITBITS+RESTBITS) = WORDS(100) | 4 |

`hashsize(r)` (bytes of hash carried after round r, for r=0..WK):
16, 13, 10, 7, 4, 1 (round 5 = WK, i.e. digitK round, carries 1 byte —
only used for the final `htlayout_equal` 32-bit-word tail comparison,
see below).

`hashwords(bytes)` = ceil(bytes/4): 4, 4, 3, 2, 1, 1 for the same rounds.

### `#if`/`#elif` branches actually taken at (144,4)

Confirmed by grep — these are the *only* branches relevant to this
param set; all other `#elif`s (WN==200 cases, WN==96) are dead code at
this specialization and were not ported:

- `getxhash0` (line ~437): `WN==144 && RESTBITS==4` →
  `pslot->hash->bytes[htl->prevbo] & 0xf` (low nibble of the first
  hash byte after the byte-offset `prevbo`).
- `getxhash1` (line ~450): `WN==144 && RESTBITS==4` → same expression,
  `pslot->hash->bytes[htl->prevbo] & 0xf`. (Byte-identical to
  `getxhash0`'s (144,4) branch — both take the low nibble of the same
  byte position; this is a genuine property of the source, not a typo
  I introduced.)
- `equi_digit0` bucketid (line ~545): `BUCKBITS==20 && RESTBITS==4` →
  `bucketid = (((u32)ph[0]<<8)|ph[1])<<4 | ph[2]>>4` — top 20 bits of
  the 3-byte hash prefix `ph[0..3]`. (Note: xhash is NOT separately
  extracted in this branch, unlike the `BUCKBITS==12 && RESTBITS==4`
  branch above it — at (144,4) the low nibble of bucketid computation
  consumes all of ph[0..3]'s top 20 bits, and getxhash0/1 later derive
  the rest-bits directly from the stored hash bytes via `prevbo`, not
  from a separately-stored xhash. This is consistent: BUCKBITS(20) +
  RESTBITS(4) = 24 = DIGITBITS, so ph[0..3] (24 bits) exactly covers
  one digit's worth of hash bits, split 20/4 between bucketid and rest.)
- `equi_digitodd`/`equi_digiteven` xorbucketid (lines ~593, ~645):
  both use the *same* branch `WN==144 && BUCKBITS==20 && RESTBITS==4` →
  `xorbucketid = ((((u32)(b0[pb+1]^b1[pb+1])<<8) | (b0[pb+2]^b1[pb+2]))<<4) | (b0[pb+3]^b1[pb+3])>>4`
  where `pb` = `htl.prevbo`. Note this reads bytes at offsets
  `prevbo+1..prevbo+3` (i.e. skips the byte at `prevbo` itself, which
  is the byte `getxhash0/1` already consumed as the rest-bits nibble)
  — again exactly 20 bits (top nibble of byte+1 through all of byte+3
  down to its own top nibble... actually: byte+1 full (8) + byte+2
  full (8) + byte+3 top nibble (4) = 20 bits). Matches BUCKBITS=20.

### Algorithm structure (single-core driver, matching `tromp.rs::worker`)

1. `equi_setstate`: clone base BLAKE2b state (personalized
   `"ZcashPoW" || WN.to_le_bytes() || WK.to_le_bytes()`, digest length
   HASHOUT=54, already absorbed `input || nonce`) into `eq`, zero
   `nslots[0]`, clear slot-fullness counters, `nsols=0`.
2. `equi_digit0(eq, 0)`: for each block index `0..NBLOCKS`, clone the
   base state, absorb `block.to_le_bytes()` (4 bytes LE), finalize to
   get 54 bytes = 3 sub-hashes of 18 bytes (WN/8) each. For each of the
   3 sub-hashes: compute `bucketid` (20 bits, top of the 24-bit digit),
   get next free slot in that bucket (bounded by NSLOTS=64, else count
   as `bfull` and drop), store `attr = tree_from_idx(block*3 + i)` (the
   raw leaf index into the 2^25-ish hash space — NHASHES=2*BASE=2^25)
   and store the remaining hash bytes (`HASHOUT_bytes[WN/8-hashbytes..]`
   i.e. the low `hashsize(0)=16` bytes of the 18-byte sub-hash) at a
   byte offset `nextbo` inside a 4-word (`HASHWORDS0=4`) slot.
3. Rounds `r=1..WK` (WK=5, so r=1,2,3,4), alternating
   `equi_digitodd`(r odd: r=1,3)/`equi_digiteven`(r even: r=2,4):
   for each bucket, for each pair of slots in the previous round's
   bucket sharing the same "xhash" (rest-bits nibble, found via
   collision-bucketing on `nxhashslots`/`xhashslots` arrays keyed by
   0..15), XOR their remaining hash-words together (skipping the
   already-matched `dunits` leading words), and skip pairs whose full
   remaining hash also collides in the last already-matched word
   (`htlayout_equal`, `hfull` counter) — those would produce a
   duplicate-index solution branch and are pruned. Store the XOR result
   into the new digit's bucket (indexed by `xorbucketid`, again bounded
   by NSLOTS, else `bfull`), with `attr = tree_from_bid(bucketid, s0, s1)`
   encoding parent bucket + both slot ids into one u32 (bitpacked:
   `((bucketid<<SLOTBITS | s0)<<SLOTBITS) | s1`, no `SLOTDIFF`/`XBITMAP`
   variants active in this build since neither macro is defined by
   `build.rs`).
4. `equi_digitK(eq, 0)`: same collision-bucketing as digitodd (WK=5 is
   odd → `getxhash0`, `trees0`), but instead of storing a new digit,
   any collision where `htlayout_equal` holds (the *full* remaining
   hash, now hashsize(WK)=1 byte, i.e. within the final word, is zero)
   is a Wagner-condition-satisfying candidate: call `candidate(eq, tree)`.
5. `candidate`: recursively unfold the binary tree of `tree`
   (`bid_s0_s1` bitpacked triples) back down through `listindices0`/
   `listindices1` to the 32 leaf indices (`listindices1` reads
   `trees0[(r-1)/2]`, i.e. the odd/even split of storage mirrors the
   round parity — WK=5 is odd so `listindices1(eq, WK, t, prf)` is
   called first per the `// assume WK odd` comment, consistent with
   WK=5 for our param set), sorts them, rejects if any duplicate
   (`prf[i] <= prf[i-1]` after full ascending sort) — this is a
   correctness *filter* on top of the recursive reconstruction (not a
   proof that the raw indices already come out sorted — `orderindices`
   only locally swaps left/right subtree halves at each merge based on
   comparing the two halves' first elements, so the final sequence is a
   tree-order interleaving, and the explicit `qsort` + strict-increase
   check in `candidate` is what actually enforces global strict
   ordering before accepting) — then appends to `eq->sols[]` (capped at
   MAXSOLS=8, but `nsols` itself is uncapped and returned to the
   caller; the harness/port must handle nsols possibly exceeding
   MAXSOLS by ignoring solutions past index MAXSOLS, matching the C
   struct's fixed-size backing array).

### Storage layout note (relevant to safe Rust design, not just C)

`htalloc`/`alloctrees` in the C uses a single-allocation, unit-typed
(`u32`-granularity) arena with the trees for round r sharing memory
with round r-4 (comment diagram in `equi_miner.c` lines ~189-199) to
avoid separate allocations. This is a memory-reuse optimization, not a
correctness requirement — the algorithm is round-sequential (round r
only ever reads round r-1's data, never anything older) so it is safe
to port this as one plain `Vec`-backed bucket array per parity
(`trees0`, `trees1`, i.e. two arrays total, each holding NBUCKETS
buckets of NSLOTS slots) that gets overwritten/reused directly across
rounds, rather than replicating the C's exact interleaved-arena
addressing scheme. Chose this simplification deliberately: byte-exact
*output* (index sets) is the port's target per `SOLVER_CORPUS.md`'s
exit criteria, not byte-exact internal memory layout.

### Planned Rust module shape

- `src/lib.rs`: `pub fn solve_144_4(input: &[u8], nonce: &[u8]) -> Vec<Vec<u32>>`
  (returns raw index-set solutions, `Vec<u32>` of length 32 each, one
  per solution found — mirrors `tromp.rs::worker`'s return type before
  the `minimal_from_indices` compression step, since RZ targets the raw
  index set first per `SOLVER_CORPUS.md`'s "Byte-exact target" section).
  All constants above hardcoded as Rust `const`s (no const generics —
  single specialization per the task's explicit instruction to keep
  this pass simple).
  Base BLAKE2b state built via the `blake2b_simd` crate (already a
  dependency in `Cargo.toml`), matching `src/blake2b.rs`/`verify.rs`'s
  personalization exactly.

## Step 2: `src/lib.rs` written, compiles

Done. `cargo build --lib` succeeds cleanly (no warnings) as of this
checkpoint. `cargo test --lib` also passes: 2 unit tests
(`constants_match_derivation` sanity-checks the derived constant table
above; `runs_without_panicking` runs `solve_144_4` once on the same
input used in the earlier manual C-harness run reported by the user,
with an all-zero 28-byte nonce, and just asserts it doesn't panic — took
~80s in a debug build, dominated by ~11.18M single-threaded BLAKE2b
calls in `digit0`, which matches the C's `NBLOCKS` exactly and is
expected at debug opt level).

Note: `cargo build` (whole workspace, all targets) still fails at this
point because `src/bin/rz_gen.rs` doesn't exist yet (step 3) — this is
expected and not a regression; `cargo build --lib` / `cargo test --lib`
are the correct scoped commands until step 3 lands.

Implementation notes worth recording:
- Did not replicate the C's `htalloc` arena-sharing memory layout
  (rounds `r` and `r-4` sharing backing memory) — used one plain `Vec`
  of `[Slot; NSLOTS]` per bucket per digit-parity-array slot instead
  (`trees0: Vec<Digit>` len 3, `trees1: Vec<Digit>` len 2, matching the
  C's `(WK+1)/2=3` and `WK/2=2` sizing). Justified in STATUS.md step 1
  notes: round r only ever reads round r-1's data, so this is a safe
  simplification that doesn't change output.
  - Note for later: this Vec-of-arrays allocates all of `trees0`/
    `trees1` up front (5 arrays x NBUCKETS x NSLOTS x ~32 bytes/slot -
    already several GB total). Worked fine for a single (144,4) run
    but is much less memory-conscious than the C's arena; if this ever
    gets extended past this pass's scope (e.g. to (200,8)/(200,9)),
    revisit before assuming it'll just work at those larger NBUCKETS.
- `Slot` stores its hash as `[u32; 4]` (max words needed at any round
  for this param set) and exposes both word-level (`hash_word`/
  `set_hash_word`, used for the XOR steps, matching the C's
  `hashunit.word` access) and byte-level (`hash_bytes`/`hash_byte`,
  used for `getxhash`/`xorbucketid`/`digit0`'s memcpy, matching the
  C's `hashunit.bytes[]` access) views over the same little-endian
  bytes, mirroring the C's `union hashunit { u32 word; uchar bytes[4]; }`
  exactly (x86/ARM64 are both little-endian, matching the union's
  implicit assumption in the original C, which also only ever targeted
  little-endian hosts in practice for this crate).
- `getxhash0`/`getxhash1` are unified into one `getxhash` helper per
  the STATUS.md step-1 finding that both branches are byte-identical
  at (144,4).
- `MAXSOLS=8` cap on physical solution storage replicated exactly:
  `Equi::nsols` counts all candidates found (unbounded), but
  `Equi.sols` (returned to the caller) only ever holds the first 8.

## Step 3: `src/bin/rz_gen.rs`

Done. `cargo build` (whole workspace) now succeeds cleanly. Manually
verified in release mode against the exact input/nonce the user ran by
hand earlier in this session
(input=`00112233445566778899aabbccddeeff...` repeated x4,
nonce=28 zero bytes):

```
C   (rz_xcheck_144_4): {"indices":[3272509,28842369,16080613,26565775,4282600,22950650,17197582,19898675,9949748,29580954,15396292,24152606,10762576,20592093,24861232,25918608,3532336,4411864,8240890,32517088,6404568,28974909,22364813,25770721,10597618,30283592,10889747,23651571,14867779,23208059,29639846,30303456]}
Rust (rz_gen):          {"indices":[3272509,28842369,16080613,26565775,4282600,22950650,17197582,19898675,9949748,29580954,15396292,24152606,10762576,20592093,24861232,25918608,3532336,4411864,8240890,32517088,6404568,28974909,22364813,25770721,10597618,30283592,10889747,23651571,14867779,23208059,29639846,30303456]}
```

Byte-identical, including index order (not just as a set) -- on the
first attempt, no debugging needed. Timing at `--release`: C ~3.9s
wall, Rust ~4.8s wall for this one (input, nonce) pair (single run,
dominated by ~11.18M BLAKE2b finalizations in `digit0`; not a
formal benchmark, just noting the port isn't wildly slower than the
reference C).

## Step 4: `tests/cross_check.rs`

Done. Written to compare the Rust port's solutions against
`rz_xcheck_144_4` (path from `env!("RZ_XCHECK_BIN_144_4")`, set by
`build.rs` via `cargo:rustc-env`) across 3 distinct nonces, comparing
as index *sets* (BTreeSet per solution, sorted list of sets per nonce)
-- matching the exit criteria's "raw index set" byte-exact target.

Nonces used (all with the same repeated-byte-pattern 64-byte input
already hand-verified by the user):
1. `00...00` (28 zero bytes) -- 1 solution. Matches the byte-identical
   result already confirmed in step 3.
2. `01010101...` (28 bytes of 0x01) -- 3 solutions.
3. `00...002a` (28 bytes, last byte 0x2a) -- 1 solution.

Pre-flight check: ran the C oracle directly on nonces 2 and 3 before
writing the assertions, to confirm neither is vacuous (zero-solution)
before trusting `cargo test` to validate them -- both produced >=1
solution.

**Result: PASS for all 3 nonces**, `cargo test --release --test
cross_check`: `test cross_check_three_nonces ... ok` (single test
function iterating all 3 nonces; would report which nonce failed via
the assertion message if any did -- none did). Wall time ~24s in
release mode (dominated by 6 total solver runs: 3 nonces x {Rust, C}).

Also ran plain `cargo test --test cross_check` (debug profile, matching
the task's literal "cargo test passes" done-condition) in the
background given debug-mode solver runs take ~80s+ each (6 runs ~
8-10+ min total) -- result to be recorded here once it completes.

## Step 3.5: `cargo test` (debug, whole workspace) -- final done-condition check

**PASS.** `cargo test --test cross_check` (debug profile):
`test cross_check_three_nonces ... ok`, `finished in 250.61s` (~4.2 min
-- 6 solver runs at debug opt level, each dominated by ~11.18M
single-threaded BLAKE2b finalizations in `digit0`; consistent with the
~80s-per-run figure already seen in the `src/lib.rs` unit test).

`cargo build` (whole workspace, all targets: lib + `rz_gen` bin +
integration test) also re-confirmed clean with no errors or warnings
after this test run.

This satisfies the task's literal done-condition ("cargo test passes in
rz/ for (144,4) across at least 3 nonces") using the plain debug
profile, not just `--release`.

## Step 5: `README.md`

Done. States the 2024-01-04 (`45652a21a`) import / 2024-01-11
(`b737d0fe2`) threading-removal provenance with commit hashes, cites
`~/Work/ZK/Requihash/SOLVERS.md` §5, states clearly this targets the
vendored solve path (not `verify.rs`/`minimal.rs`), and states clearly
this pass covers `(144,4)` only with `(200,8)`/`(200,9)` as explicit
follow-on work not yet done.

## Final summary (all steps complete)

| Step | Status |
|---|---|
| 1. Algorithm understanding written to STATUS.md before any Rust | Done |
| 2. `src/lib.rs` (no I/O, `(144,5,4)` hardcoded), compiles | Done, no warnings |
| 3. `src/bin/rz_gen.rs`, JSON output matches C harness shape | Done, byte-identical output confirmed manually |
| 4. `tests/cross_check.rs`, 3 nonces vs C oracle | Done, PASS on all 3 (both debug and release profiles) |
| 5. `README.md` | Done |

`cargo test` (plain, debug profile, whole workspace) passes. `cargo
build` (whole workspace) is clean with zero warnings.

## Open questions / things to flag for later (not blockers, just noted)

- `getxhash0` and `getxhash1` are byte-identical expressions at (144,4)
  specifically (`bytes[prevbo] & 0xf` in both). Not yet checked whether
  this coincidence holds at (200,8)/(200,9) too, or is specific to
  (144,4)'s bit-width arithmetic (BUCKBITS=20 + RESTBITS=4 = DIGITBITS
  exactly, with no leftover bits split across the byte boundary the way
  the 200-bit variants have). Worth a second look when/if (200,*) is
  ported.
- The C's `htalloc`/`alloctrees` arena-sharing memory layout (rounds r
  and r-4 sharing backing memory, per the ASCII diagram at
  `equi_miner.c` lines ~189-199) was deliberately not replicated in the
  Rust port -- used plain per-round `Vec` storage instead, justified by
  the algorithm being strictly round-sequential. This means the Rust
  port's peak memory usage is higher than the C's (all `trees0`/
  `trees1` slots for the whole run allocated up front rather than
  reused). Not a correctness issue at (144,4) -- ran fine -- but flagged
  because it would matter more at (200,8)/(200,9)'s much larger
  NBUCKETS if this port is ever extended there.
- `MAXSOLS=8` caps physical solution storage in both the C and this
  port, but the *count* (`nsols`) is uncapped in both. None of the 3
  test nonces exercised this edge (max seen was 3 solutions for one
  nonce), so the `soli < MAXSOLS` guard in `Equi::candidate` is
  written to match the C's behavior by inspection but not exercised by
  a test that actually produces >8 solutions for one nonce.
- No fuzzing / property-testing beyond the 3 fixed nonces was done in
  this pass (matches the task's literal ask: "at least 3 distinct
  nonces").

## Step 5: `README.md`

Not yet started.

## Open questions / things to flag later

- `getxhash0` and `getxhash1` are byte-identical expressions at (144,4)
  (both `bytes[prevbo] & 0xf`). Worth a second look at (200,*) variants
  later to see if this coincidence is (144,4)-specific or general —
  not investigated since out of scope this pass.
- `MAXSOLS=8` caps `eq->sols[]` storage but not `eq->nsols` itself (an
  unbounded counter) — the Rust port must replicate "count all,
  physically store only the first 8" to stay byte-identical to the C
  if a test nonce happens to yield >8 solutions (unlikely at (144,4)'s
  small proof size but not impossible; flagging in case a cross-check
  nonce trips this).
