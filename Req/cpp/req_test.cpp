// Requihash C++ self-test: solve small parameters, verify the solutions round-trip
// through minimal encoding, and confirm the regularity check rejects tampered
// (single-list-style) index vectors.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "requihash.h"
#include "solver.h"

using namespace requihash;

static std::vector<unsigned char> bytes(const std::string& s) {
    return std::vector<unsigned char>(s.begin(), s.end());
}

static int g_fail = 0;
#define CHECK(cond, msg)                                        \
    do {                                                        \
        if (!(cond)) { printf("FAIL: %s\n", msg); g_fail++; }   \
        else { printf("ok: %s\n", msg); }                       \
    } while (0)

static bool solve_and_verify(unsigned int n, unsigned int k, const std::string& tag) {
    Params P(n, k);
    Requihash eh(P);
    auto input = bytes("requihash-test-block-header");
    // search nonces until a solution appears (small params solve quickly)
    for (uint32_t ni = 0; ni < 2000; ni++) {
        std::vector<unsigned char> nonce(4);
        nonce[0] = ni & 0xFF; nonce[1] = (ni >> 8) & 0xFF;
        nonce[2] = (ni >> 16) & 0xFF; nonce[3] = (ni >> 24) & 0xFF;
        auto base = eh.InitialiseState(input, nonce);
        auto sols = Solve(eh, base);
        if (sols.empty()) continue;
        for (auto& s : sols) {
            if (!IsValidSolution(eh, base, s)) {
                printf("FAIL[%s]: solver produced a solution the verifier rejects\n",
                       tag.c_str());
                return false;
            }
            // minimal-encoding round trip
            auto minimal = GetMinimalFromIndices(s, P.CollisionBitLength());
            if (minimal.size() != P.SolutionWidth()) {
                printf("FAIL[%s]: minimal size %zu != SolutionWidth %zu\n", tag.c_str(),
                       minimal.size(), P.SolutionWidth());
                return false;
            }
            auto back = GetIndicesFromMinimal(minimal, P.CollisionBitLength());
            if (back != s) {
                printf("FAIL[%s]: minimal encoding round-trip mismatch\n", tag.c_str());
                return false;
            }
            // tamper: swap two leaves so the leaf->class mapping is wrong -> must reject
            if (s.size() >= 2) {
                auto t = s;
                std::swap(t[0], t[1]);
                // swapping breaks canonical ordering and/or collision structure
                bool rej = !IsValidSolution(eh, base, t);
                if (!rej) {
                    // A swap that happens to still validate would be a false accept;
                    // for the ordering check it should essentially always reject.
                    printf("WARN[%s]: swapped-leaf vector still validated\n", tag.c_str());
                }
            }
            printf("ok: [%s] solved (nonce=%u), %zu indices, minimal=%zu bytes\n",
                   tag.c_str(), ni, s.size(), minimal.size());
            return true;
        }
    }
    printf("FAIL[%s]: no solution found within nonce budget\n", tag.c_str());
    return false;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);  // unbuffered progress
    printf("== Requihash C++ tests ==\n");

    // BLAKE2b sanity: known-answer for empty input, 64-byte output, no person.
    {
        unsigned char person[16];
        memset(person, 0, 16);
        auto s = reqb2::init(64, person);
        unsigned char out[64];
        reqb2::final(s, out);
        // BLAKE2b-512("") = 786a02f7...  (first 4 bytes 0x78 0x6a 0x02 0xf7)
        bool ok = out[0] == 0x78 && out[1] == 0x6a && out[2] == 0x02 && out[3] == 0xf7;
        CHECK(ok, "BLAKE2b-512 empty-input known-answer prefix");
    }

    // Parameter validity: k <= sqrt(n/2+1) bound is informational; engine rejects
    // n not divisible by (k+1) and k>=n.
    {
        bool threw = false;
        try { Params bad(200, 200); } catch (...) { threw = true; }
        CHECK(threw, "Params rejects k >= n");
    }

    // Fast round-trip params (n divisible by 8 and by k+1):
    //   (48,5): ell=8, 512 leaves;  (72,5): ell=12, 8192 leaves.
    // Both solve within a few nonces; (96,5) with 2^17 leaves is exercised by the
    // separate cross-check generator, not the unit test's nonce search.
    CHECK(solve_and_verify(48, 5, "48,5"), "solve+verify (48,5)");
    CHECK(solve_and_verify(72, 5, "72,5"), "solve+verify (72,5)");

    // Seam agreement: arena solver == reference solver, and all three verifiers
    // agree (accept real, reject tampered).
    for (auto [n, k] : {std::pair<unsigned,unsigned>{48,5}, {72,5}}) {
        Params P(n, k);
        Requihash eh(P);
        auto input = bytes("seam-check");
        bool solver_ok = true, verifier_ok = true;
        for (uint32_t ni = 0; ni < 20; ni++) {
            std::vector<unsigned char> nonce(4);
            nonce[0]=ni&0xFF; nonce[1]=(ni>>8)&0xFF; nonce[2]=(ni>>16)&0xFF; nonce[3]=(ni>>24)&0xFF;
            auto base = eh.InitialiseState(input, nonce);
            auto ref = Solve(eh, base);
            auto are = SolveArena(eh, base);
            std::sort(ref.begin(), ref.end());
            std::sort(are.begin(), are.end());
            if (ref != are) { solver_ok = false; break; }
            for (auto& s : are) {
                if (!IsValidSolution(eh, base, s) || !IsValidSolutionEarly(eh, base, s))
                    verifier_ok = false;
                if (s.size() >= 2) {
                    auto t = s; std::swap(t[0], t[1]);
                    if (IsValidSolution(eh, base, t) || IsValidSolutionEarly(eh, base, t))
                        verifier_ok = false;
                }
            }
        }
        char msg[64];
        snprintf(msg, sizeof msg, "arena==reference solver (%u,%u)", n, k);
        CHECK(solver_ok, msg);
        snprintf(msg, sizeof msg, "all verifiers agree (%u,%u)", n, k);
        CHECK(verifier_ok, msg);
    }

    // Paper Table 3 wire sizes at Zcash production params (200,9):
    // Equihash minimal = 1344 B, Requihash compact = 1280 B.
    {
        Params P(200, 9);
        CHECK(P.SolutionWidth() == 1344, "Equihash-compatible width (200,9) == 1344 B");
        CHECK(P.CompactWidth() == 1280, "Requihash compact width (200,9) == 1280 B (Table 3)");
    }

    if (g_fail == 0) printf("\nALL PASS\n");
    else printf("\n%d FAILURES\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
