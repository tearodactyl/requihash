#!/usr/bin/env python3
"""Extract official Zcash Equihash known-answer vectors from the vendored
`equihash` crate's Rust source (src/test_vectors/{valid,invalid}.rs) into a
plain JSON intermediate. The crate's TestVector structs are pub(crate), so
they can't be imported directly -- this parses the literal arrays out of the
source text instead (bracket-depth-aware block splitting, hex/decimal-aware
integer parsing).

Usage:
    python3 extract_zcash_kat.py <path-to-equihash-crate-dir> <output.json>

Example:
    python3 extract_zcash_kat.py \\
        ~/.cargo/registry/src/index.crates.io-*/equihash-0.3.0 \\
        raw_vectors.json

Output JSON: {"valid": [{"n","k","input","nonce","solutions"}, ...],
              "invalid": [{"n","k","input","nonce","solution","error"}, ...]}
All byte fields are hex strings; solutions/solution are lists of u32 indices.

See minimal_encode.rs in this directory for the next pipeline step (computing
minimal_hex via this repo's own encoder) and assemble_vectors.py for the final
step (writing Req/vectors/*.json in this repo's schema).
"""
import re
import sys
import json


def parse_int_list(s):
    s = s.replace("\n", "")
    out = []
    for tok in s.split(","):
        tok = tok.strip()
        if not tok:
            continue
        out.append(int(tok, 0))  # base 0 autodetects 0x prefix
    return out


def parse_nonce(text):
    if re.search(r"nonce:\s*\[0;\s*32\]", text):
        return [0] * 32
    m = re.search(r"nonce:\s*\[([\dxXa-fA-F,\s]+)\]", text)
    return parse_int_list(m.group(1))


def split_top_level_structs(text, start_marker):
    """Find each `start_marker { ... }` at bracket-balance-aware boundaries."""
    structs = []
    i = 0
    while True:
        i = text.find(start_marker, i)
        if i == -1:
            break
        brace_start = text.index("{", i)
        depth = 0
        j = brace_start
        while j < len(text):
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        structs.append(text[brace_start + 1 : j])
        i = j + 1
    return structs


def parse_block(text):
    n = int(re.search(r"n:\s*(\d+)", text).group(1))
    k = int(re.search(r"k:\s*(\d+)", text).group(1))
    input_m = re.search(r'input:\s*b"((?:[^"\\]|\\.)*)"', text)
    input_bytes = input_m.group(1).encode().decode("unicode_escape").encode("latin1")
    nonce_vals = parse_nonce(text)
    assert len(nonce_vals) == 32, f"bad nonce len {len(nonce_vals)}"
    return n, k, input_bytes, nonce_vals


def parse_valid_file(path):
    text = open(path).read()
    blocks = split_top_level_structs(text, "\n    TestVector {")
    vectors = []
    for b in blocks:
        n, k, input_bytes, nonce = parse_block(b)
        sol_text = b[b.index("solutions:") :]
        sol_lists = re.findall(r"&\[([\dxXa-fA-F,\s]+)\]", sol_text)
        solutions = [parse_int_list(sl) for sl in sol_lists]
        vectors.append(
            {
                "n": n,
                "k": k,
                "input": input_bytes.hex(),
                "nonce": bytes(nonce).hex(),
                "solutions": solutions,
            }
        )
    return vectors


def parse_invalid_file(path):
    text = open(path).read()
    blocks = split_top_level_structs(text, "\n    TestVector {")
    vectors = []
    for b in blocks:
        n, k, input_bytes, nonce = parse_block(b)
        sol_m = re.search(r"solution:\s*&\[([\dxXa-fA-F,\s]+)\]", b)
        idxs = parse_int_list(sol_m.group(1))
        err_m = re.search(r"error:\s*Kind::(\w+)", b)
        vectors.append(
            {
                "n": n,
                "k": k,
                "input": input_bytes.hex(),
                "nonce": bytes(nonce).hex(),
                "solution": idxs,
                "error": err_m.group(1),
            }
        )
    return vectors


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    crate_dir, out_path = sys.argv[1], sys.argv[2]

    valid = parse_valid_file(f"{crate_dir}/src/test_vectors/valid.rs")
    invalid = parse_invalid_file(f"{crate_dir}/src/test_vectors/invalid.rs")

    print(f"valid: {len(valid)} entries, total solutions: {sum(len(v['solutions']) for v in valid)}")
    print(f"invalid: {len(invalid)} entries")
    for v in valid:
        exp_len = 2 ** v["k"]
        for s in v["solutions"]:
            assert len(s) == exp_len, f"n={v['n']} k={v['k']} expected {exp_len} indices got {len(s)}"
        print(f"  n={v['n']} k={v['k']} nonce0={v['nonce'][:4]} solutions={len(v['solutions'])}")
    for v in invalid:
        print(f"  INVALID n={v['n']} k={v['k']} error={v['error']} sol_len={len(v['solution'])}")

    json.dump({"valid": valid, "invalid": invalid}, open(out_path, "w"))
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
