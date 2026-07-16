# Vendored portable BLAKE2b reference implementation

Files `blake2.h`, `blake2-impl.h`, `blake2b-ref.c` copied **unmodified**
from the `ref/` directory of github.com/BLAKE2/BLAKE2 at commit
`ed1974e` (2023-02-12, upstream tip as of vendoring on 2026-07-16; local
clone `~/Work/ZK/ZKs/BLAKE/blake2-reference`). Author: Samuel Neves;
license: CC0 1.0 / OpenSSL License / Apache-2.0, at your option (header
of each file).

This is the canonical BLAKE2b implementation for every C/C++ consumer in
this repository (decision record: `../../BLAKE.md`). Build-time
consumers reference this directory **repo-relative** — never by absolute
path. To update: re-copy from the clone, record the new commit here, and
re-run every consumer's tests (`rz`, `cs`, `rk/original`).
