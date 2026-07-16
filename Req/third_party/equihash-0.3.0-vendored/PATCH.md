# PATCH.md — what changed from the pinned `equihash 0.3.0` crate

Source: `equihash-0.3.0` as published to crates.io, copied from cargo's
own registry cache (`~/.cargo/registry/src/index.crates.io-*/equihash-0.3.0`).
Same crate RZ (`Req/SOLVER_CORPUS/rz/`) and A14 (`Req/vectors/zcash_kat_*.json`)
already depend on elsewhere in this project — this is a second, separately
patched copy, not a replacement for those uses.

**Why a vendored copy exists at all**: `expand_array`/`compress_array`/
`minimal_from_indices`/`indices_from_minimal` (in `src/minimal.rs`) and
`Params` (in `src/params.rs`) are `pub(crate)` upstream — real,
zcashd-derived bit-packing logic (`minimal.rs`'s own comments cite exact
`zcash/zcash` `equihash.cpp` line ranges) that this project's A19 task
needed to call directly, to verify `Req/rust/src/lib.rs`'s own
`expand_array`/`compress_array`/`get_minimal_from_indices` against it
byte-for-byte. `pub(crate)` items are not reachable through a normal
path or crates.io dependency. Editing cargo's own registry cache in
place was rejected: that cache is content-hash-verified and shared
across every project on this machine that depends on `equihash 0.3.0`
(including this project's own RZ/A14 uses) — a local edit there would
either be silently reverted by cargo or leak into unrelated builds.
Vendoring a separate copy and widening visibility only in that copy is
the standard, safe way to do this.

## Exact changes

Four `pub(crate)` -> `pub` visibility widenings, nothing else — no logic
touched, no lines added or removed beyond the visibility keyword itself.

**`src/lib.rs`**: `mod minimal;` / `mod params;` -> `pub mod minimal;` /
`pub mod params;` (so the now-public items inside are actually reachable
from outside the crate).

**`src/params.rs`**: `Params` struct, its two fields (`n`, `k`), `Params::new`,
and `Params::collision_bit_length` widened to `pub`. (`hash_output`,
`collision_byte_length`, `hash_length`, `indices_per_hash_output` stay
`pub(crate)` — not needed by the A19 comparison.)

**`src/minimal.rs`**: `compress_array`, `expand_array`, `minimal_from_indices`,
`indices_from_minimal` widened to `pub`. `compress_array`/`minimal_from_indices`
keep their existing `#[cfg(any(feature = "solver", test))]` gate — build
this crate with `--features solver` (or as a dependency with
`features = ["solver"]`) to reach them.

Full diff (`diff -ru` against the unmodified registry cache) reproduced
here for a permanent record, since a future `cargo update`/re-vendor
could otherwise lose track of exactly what this copy changed:

```diff
--- src/lib.rs (upstream)
+++ src/lib.rs (vendored)
@@ -32,8 +32,8 @@
 #[macro_use]
 extern crate alloc;

-mod minimal;
-mod params;
+pub mod minimal;
+pub mod params;
 mod verify;

--- src/params.rs (upstream)
+++ src/params.rs (vendored)
@@ -1,12 +1,12 @@
 #[derive(Clone, Copy)]
-pub(crate) struct Params {
-    pub(crate) n: u32,
-    pub(crate) k: u32,
+pub struct Params {
+    pub n: u32,
+    pub k: u32,
 }

 impl Params {
     /// Returns `None` if the parameters are invalid.
-    pub(crate) fn new(n: u32, k: u32) -> Option<Self> {
+    pub fn new(n: u32, k: u32) -> Option<Self> {
         ...
     pub(crate) fn hash_output(&self) -> u8 {
         (self.indices_per_hash_output() * self.n / 8) as u8
     }
-    pub(crate) fn collision_bit_length(&self) -> usize {
+    pub fn collision_bit_length(&self) -> usize {
         (self.n / (self.k + 1)) as usize
     }
     pub(crate) fn collision_byte_length(&self) -> usize {

--- src/minimal.rs (upstream)
+++ src/minimal.rs (vendored)
@@ -7,7 +7,7 @@
 // Rough translation of CompressArray() from: ...
 #[cfg(any(feature = "solver", test))]
-fn compress_array(array: &[u8], bit_len: usize, byte_pad: usize) -> Vec<u8> {
+pub fn compress_array(array: &[u8], bit_len: usize, byte_pad: usize) -> Vec<u8> {
     ...
-pub(crate) fn expand_array(vin: &[u8], bit_len: usize, byte_pad: usize) -> Vec<u8> {
+pub fn expand_array(vin: &[u8], bit_len: usize, byte_pad: usize) -> Vec<u8> {
     ...
 // Rough translation of GetMinimalFromIndices() from: ...
 #[cfg(any(feature = "solver", test))]
-pub(crate) fn minimal_from_indices(p: Params, indices: &[u32]) -> Vec<u8> {
+pub fn minimal_from_indices(p: Params, indices: &[u32]) -> Vec<u8> {
     ...
 /// Returns `None` if the parameters are invalid for this minimal encoding.
-pub(crate) fn indices_from_minimal(p: Params, minimal: &[u8]) -> Option<Vec<u32>> {
+pub fn indices_from_minimal(p: Params, minimal: &[u8]) -> Option<Vec<u32>> {
```

## Used by

`Req/rust/src/lib.rs` tests `expand_compress_array_round_trips_against_pinned_equihash_crate`
and `get_minimal_from_indices_matches_pinned_equihash_crate` (`Req/PLAN.md` A19),
via the `equihash` dev-dependency in `Req/rust/Cargo.toml` pointing at this
directory with `features = ["solver"]`.

## License

MIT OR Apache-2.0, unchanged from upstream — see `Cargo.toml`. This is a
vendored copy of open-source code with a visibility change, not a
derivative work in any sense that affects licensing.
