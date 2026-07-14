#!/usr/bin/env python3
"""Assemble Req/vectors/*.json from extract_zcash_kat.py's raw JSON plus
req_vecencode's `n k minimal_hex` stdout lines.

Usage:
    python3 tools/extract_zcash_kat.py <crate-dir> raw_vectors.json
    python3 tools/build_encode_input.py raw_vectors.json encode_input.txt
    cargo run --release --bin req_vecencode --manifest-path rust/Cargo.toml \\
        < encode_input.txt > encoded_output.txt
    python3 tools/assemble_vectors.py raw_vectors.json encoded_output.txt vectors/ [crate-version]

crate-version (optional, e.g. "0.3.0") is recorded in each vector's `source`
field for provenance; omit it if unknown.

This writes vectors/zcash_kat_<n>_<k>.json (valid, keying=single) and
vectors/zcash_kat_invalid.json (adversarial, expect_reject=true) in this
repo's vector schema (SPEC.md section 9), tagged with a `source` field.

Why keying=single: these are official Zcash plain-Equihash KATs (no `i mod k`
regularity term), not Requihash vectors -- see Req/PLAN.md item A14. Neither
this repo's Rust engine nor its C++ header implements single-list keying yet,
so these vectors are not verified end-to-end by any backend here today; they
are the correctness oracle for a future ported index-pointer solver (A6),
which will itself need to be single-list to match upstream Equihash.
"""
import sys
import json
from pathlib import Path


def build_encode_input(raw):
    """Emit 'n k idx0,idx1,...' lines for every valid solution, in a fixed
    order that assemble() below must replay when consuming encoded output."""
    lines = []
    for v in raw["valid"]:
        n, k = v["n"], v["k"]
        for sol in v["solutions"]:
            lines.append(f"{n} {k} " + ",".join(str(i) for i in sol))
    return lines


def main():
    if len(sys.argv) not in (4, 5):
        print(__doc__)
        sys.exit(1)
    raw_path, encoded_path, out_dir = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    version_tag = f" v{sys.argv[4]}" if len(sys.argv) > 4 else ""

    raw = json.load(open(raw_path))
    encoded_lines = open(encoded_path).read().strip().split("\n")
    enc_iter = iter(encoded_lines)

    vectors_by_nk = {}
    for v in raw["valid"]:
        n, k = v["n"], v["k"]
        for sol in v["solutions"]:
            line = next(enc_iter)
            parts = line.split(" ", 2)
            assert int(parts[0]) == n and int(parts[1]) == k, (
                f"encode_input/encoded_output order mismatch: expected n={n} k={k}, "
                f"got line {line!r} -- did you regenerate encode_input.txt with the "
                f"same raw_vectors.json passed here?"
            )
            minimal_hex = parts[2]
            vec = {
                "n": n,
                "k": k,
                "input_hex": v["input"],
                "nonce_hex": v["nonce"],
                "minimal_hex": minimal_hex,
                "indices": sol,
                "hash": "blake2b",
                "m": 1,
                "keying": "single",
                "source": f"zcash/equihash crate{version_tag} test_vectors::valid (official Zcash KAT)",
            }
            vectors_by_nk.setdefault((n, k), []).append(vec)

    for (n, k), vecs in sorted(vectors_by_nk.items()):
        fname = out_dir / f"zcash_kat_{n}_{k}.json"
        json.dump(vecs, open(fname, "w"), indent=2)
        print(f"wrote {fname}: {len(vecs)} vectors")

    invalid_out = []
    for v in raw["invalid"]:
        invalid_out.append(
            {
                "n": v["n"],
                "k": v["k"],
                "input_hex": v["input"],
                "nonce_hex": v["nonce"],
                "indices": v["solution"],
                "hash": "blake2b",
                "m": 1,
                "keying": "single",
                "expect_reject": True,
                "expect_error_kind": v["error"],
                "source": f"zcash/equihash crate{version_tag} test_vectors::invalid (official Zcash KAT, adversarial)",
            }
        )
    fname = out_dir / "zcash_kat_invalid.json"
    json.dump(invalid_out, open(fname, "w"), indent=2)
    print(f"wrote {fname}: {len(invalid_out)} vectors")


if __name__ == "__main__":
    main()
