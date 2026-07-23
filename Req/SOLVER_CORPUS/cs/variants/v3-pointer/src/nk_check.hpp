// Pre-flight (n,K) validity check for CS/Sequihash drivers -- run BEFORE
// constructing a solver, so a bad point is rejected immediately with a
// message naming which rule failed, instead of a generic
// std::invalid_argument thrown deep inside solve() after the process has
// already spent time/memory getting there. Mirrors
// Req/scripts/equihash_formulas.py::cs_valid_n's own rule set exactly
// (kept in sync by hand -- see that function's docstring for the
// authoritative rule-origin documentation, not repeated here):
//
//   1. K a power of 2, K >= 2 (lgK = log2(K), K=1 is degenerate -- see
//      the k=1 corner case documented in klist_v1.cpp and elsewhere:
//      Python reference itself never checks XOR-to-zero at K=1, a
//      pre-existing reference quirk, not something this check rejects,
//      since K=1 as such is at least well-formed input)
//   2. n % 8 == 0            -- byte-alignment convenience (not
//                               algorithm-fundamental -- see rule
//                               origins in equihash_formulas.py)
//   3. n % (lgK+1) == 0      -- algorithm-fundamental (ell=n/(lgK+1)
//                               must be a whole number of bits/round)
//   4. n <= 512              -- BLAKE2b's own 64-byte digest cap
//
// Deliberately does NOT enforce Req/Equihash's additional cbl in [8,25]
// safety margin (rule 4 in the Python tool's Req-side derivation) --
// that is a Req/rust-specific implementation choice CS's own reference
// never makes; a CS driver accepting a Req-invalid point is correct
// behavior, not a gap.
#ifndef CS_NK_CHECK_HPP
#define CS_NK_CHECK_HPP

#include <cstdio>
#include <cstdlib>
#include <string>

namespace cs_common {

// Returns empty string if (n,K) is valid; otherwise a message naming
// which rule failed (matching the rule numbering in this file's own
// comment above), suitable to print and exit(1) on.
inline std::string check_nk(unsigned n, unsigned K) {
    if (K < 2) {
        return "K must be >= 2 (K=1 is degenerate -- see the k=1 corner "
               "case note in this port's klist*.cpp; not rejected outright, "
               "but almost certainly not what you meant to run)";
    }
    unsigned lgK = 0;
    unsigned kk = K;
    while (kk > 1) { kk >>= 1; ++lgK; }
    if ((1u << lgK) != K) {
        return "K must be a power of 2 (got K=" + std::to_string(K) + ")";
    }
    if (n % 8 != 0) {
        return "n must be a multiple of 8 (rule 2, byte-alignment convenience -- got n=" + std::to_string(n) + ")";
    }
    unsigned m = lgK + 1;
    if (n % m != 0) {
        return "n must be divisible by log2(K)+1=" + std::to_string(m) +
               " (rule 3, algorithm-fundamental: ell=n/(log2(K)+1) must be a whole number -- got n=" + std::to_string(n) + ")";
    }
    if (n > 512) {
        return "n must be <= 512 (rule 4, BLAKE2b's own 64-byte/512-bit digest cap -- got n=" + std::to_string(n) + ")";
    }
    return "";
}

// Convenience: checks and, on failure, prints to stderr and exits(1).
// Call this first thing in main(), before constructing any solver.
inline void require_valid_nk(unsigned n, unsigned K, const char* prog_name) {
    std::string err = check_nk(n, K);
    if (!err.empty()) {
        fprintf(stderr, "%s: invalid (n=%u, K=%u): %s\n", prog_name, n, K, err.c_str());
        fprintf(stderr, "%s: see Req/scripts/equihash_formulas.py --valid-n <k> --cs "
                        "for the full valid-n list at this K's equivalent k=log2(K)\n", prog_name);
        exit(1);
    }
}

} // namespace cs_common

#endif // CS_NK_CHECK_HPP
