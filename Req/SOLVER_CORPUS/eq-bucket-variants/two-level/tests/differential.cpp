// Differential test: for each (n,k,nonce) test point, cross-checks the
// fully-sorted-bucket solver against the independent brute-force oracle
// (../../common/brute_force.hpp, no bucketing at all) plus
// self-verification of every returned solution.
#include "brute_force.hpp"
#include "solver.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> out(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = (uint8_t)strtoul(hex.substr(i * 2, 2).c_str(), nullptr, 16);
    }
    return out;
}

struct TestPoint {
    unsigned n, k;
};

int main() {
    // Small points (brute force is O(N^2) per round, so n/k kept modest).
    // For each (n,k), scan several nonces via the brute-force oracle
    // until at least one SOLVING nonce is found -- a test that only ever
    // sees "0 solutions" from both sides validates no-false-positives
    // but not solution reconstruction itself (a buggy bucket boundary
    // could drop a real collision and this would still "pass" trivially
    // at an unlucky nonce). Every solving nonce found is cross-checked;
    // every non-solving nonce along the way is checked too (both sides
    // agree it's empty), so both directions get exercised.
    std::vector<TestPoint> points = {{40, 4}, {48, 5}, {72, 5}};

    int failures = 0;
    for (const auto& pt : points) {
        eq_common::Params p(pt.n, pt.k);
        bool found_solving_nonce = false;
        for (unsigned trial = 0; trial < 64; ++trial) {
            std::vector<uint8_t> nonce(16, 0);
            for (int b = 0; b < 16; ++b) nonce[b] = (uint8_t)((trial * 16 + b) * 37 + b);

            eq_two_level::Solver solver(pt.n, pt.k, nonce);
            auto got = solver.solve();
            auto want = eq_common::solve_brute_force(p, nonce);

            std::sort(got.begin(), got.end());
            std::sort(want.begin(), want.end());
            if (got != want) {
                fprintf(stderr, "  (n=%u,k=%u) trial %u MISMATCH: got %zu solution(s), oracle has %zu\n",
                        pt.n, pt.k, trial, got.size(), want.size());
                failures++;
                continue;
            }
            for (const auto& sol : got) {
                if (!eq_common::verify_solution(p, nonce, sol)) {
                    fprintf(stderr, "  (n=%u,k=%u) trial %u self-verification FAILED\n", pt.n, pt.k, trial);
                    failures++;
                    break;
                }
            }
            if (!want.empty()) {
                found_solving_nonce = true;
                printf("checking (n=%u, k=%u) trial %u: %zu solution(s), match oracle, self-verified\n",
                       pt.n, pt.k, trial, got.size());
            }
        }
        if (!found_solving_nonce) {
            fprintf(stderr, "  (n=%u,k=%u): no solving nonce found in 64 trials -- test is not exercising"
                            " real solution reconstruction, only no-false-positives\n", pt.n, pt.k);
            failures++;
        }
    }

    if (failures > 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("All test points matched the brute-force oracle across all trial nonces.\n");
    return 0;
}
