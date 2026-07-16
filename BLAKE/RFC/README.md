# RFC/ — RFC 7693 and its extracted reference code

- `rfc7693.txt`, `rfc7693.pdf` — "The BLAKE2 Cryptographic Hash and
  Message Authentication Code (MAC)", Saarinen & Aumasson, November 2015
  (Informational). Fetched 2026-07-16 from rfc-editor.org / ietf.org.
- `code/` — the five files extracted verbatim from the RFC's own
  `<CODE BEGINS>`/`<CODE ENDS>` fences (Appendix C: `blake2b.{h,c}`,
  Appendix D: `blake2s.{h,c}`, Appendix E: `selftest.c`), page
  headers/footers stripped, otherwise byte-faithful. **Validated**: all
  five compile as plain C99 with no flags beyond `-std=c99` and the
  RFC's own self-test passes (`blake2b_selftest() = OK`,
  `blake2s_selftest() = OK`).

Why this code is here for reference and NOT the implementation this
repository builds against: the RFC interface is
`blake2b_init(ctx, outlen, key, keylen)` — plain and keyed hashing only.
**No parameter block, no `personal`/`salt`, no tree fields.** Equihash
requires the 16-byte personalization; only the authors' package lineage
(`../vendor/blake2`) carries it. See `../BLAKE.md` for the full
comparison and the decision record.
