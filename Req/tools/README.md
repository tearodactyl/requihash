# tools/ — vector-pipeline scripts

Reusable scripts, not one-off scratch files. Currently one pipeline: pulling
official Zcash Equihash KAT vectors out of the vendored `equihash` crate and
into this repo's `vectors/*.json` schema (SPEC.md section 9).

## Pull official Zcash KAT vectors from the vendored crate

Built for Req/PLAN.md item A14. Source: the `equihash` crate (a Zebro
dependency, already in the local Cargo cache) vendors official Zcash
Equihash(96,5)/(144,5)/(200,9) known-answer vectors in
`src/test_vectors/{valid,invalid}.rs` as `pub(crate)` Rust arrays — not
importable directly, so step 1 parses the source text.

```bash
CRATE_DIR=$(find ~/.cargo/registry/src -maxdepth 1 -iname "index.crates.io-*" \
    -exec find {} -maxdepth 1 -iname "equihash-*" \; | head -1)

# 1. Parse the crate's Rust source into a plain JSON intermediate.
python3 tools/extract_zcash_kat.py "$CRATE_DIR" /tmp/raw_vectors.json

# 2. Flatten to 'n k idx0,idx1,...' lines for the Rust encoder.
python3 tools/build_encode_input.py /tmp/raw_vectors.json /tmp/encode_input.txt

# 3. Compute minimal_hex via this repo's own get_minimal_from_indices
#    (reused, not reimplemented) with a round-trip check against
#    get_indices_from_minimal -- panics loudly on any mismatch.
cargo run --release --bin req_vecencode --manifest-path rust/Cargo.toml \
    < /tmp/encode_input.txt > /tmp/encoded_output.txt

# 4. Assemble the final vectors/zcash_kat_*.json files (crate version arg
#    is optional, recorded in each vector's `source` field for provenance).
python3 tools/assemble_vectors.py /tmp/raw_vectors.json /tmp/encoded_output.txt vectors/ 0.3.0
```

**Why `keying: "single"`, deliberately:** these are official plain-Equihash
KATs (no `i mod k` regularity term), not Requihash vectors. Neither this
repo's Rust engine nor its C++ header implements single-list keying yet
(SPEC.md §1: specified, not implemented) — so these vectors aren't verified
end-to-end by any backend here today. They exist as the correctness oracle
for A6's ported index-pointer solver once single-list keying lands (that
solver will itself be single-list, matching upstream Equihash, not
Requihash's regular keying).

**Re-run this if:** the vendored crate version changes (bump would land via
Zebro's Cargo.lock) and might add/change vectors, or you want to pull a
different crate's KATs through the same pipeline (steps 1 and 4 are the only
crate-format-specific parts — swap `extract_zcash_kat.py`'s parsing for a new
source format and steps 2–3 are unchanged).
