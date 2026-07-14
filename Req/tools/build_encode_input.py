#!/usr/bin/env python3
"""Glue step: turn extract_zcash_kat.py's raw_vectors.json into the
'n k idx0,idx1,...' line format req_vecencode expects on stdin.

Usage: python3 tools/build_encode_input.py raw_vectors.json encode_input.txt
"""
import sys
import json


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    raw = json.load(open(sys.argv[1]))
    lines = []
    for v in raw["valid"]:
        n, k = v["n"], v["k"]
        for sol in v["solutions"]:
            lines.append(f"{n} {k} " + ",".join(str(i) for i in sol))
    open(sys.argv[2], "w").write("\n".join(lines) + "\n")
    print(f"{len(lines)} solutions -> {sys.argv[2]}")


if __name__ == "__main__":
    main()
