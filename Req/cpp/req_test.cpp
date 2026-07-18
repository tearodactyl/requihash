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

    // T2.3 F12/F13/F14 parameter bounds (mirrors the Rust `params_rejected`):
    // n > 512 reads past the digest; k == 0 divides by zero in the
    // regularity binding; collision bit length must be in [8, 25].
    {
        auto rejects = [](unsigned n, unsigned k) {
            try { Params bad(n, k); } catch (...) { return true; }
            return false;
        };
        CHECK(rejects(520, 4), "Params rejects n > 512 (F12)");
        CHECK(rejects(48, 0), "Params rejects k == 0 (F13)");
        CHECK(rejects(24, 5) && rejects(168, 5) && rejects(512, 3),
              "Params rejects collision bit length outside [8,25] (F14)");
        CHECK(!rejects(48, 1) && !rejects(200, 7) && !rejects(512, 31),
              "Params accepts k==1, cbl==25, and n==512 boundary instances");

        // NBounds must agree exactly with constructor acceptance, exhaustively
        // over k in [0,70], n in [1,560] (mirror of the Rust
        // n_bounds_match_constructor test).
        bool bounds_ok = true;
        for (unsigned k = 0; k <= 70 && bounds_ok; k++) {
            unsigned lo = 0, hi = 0;
            bool has = Params::NBounds(k, lo, hi);
            unsigned a = 8, b = k + 1;
            while (b != 0) { unsigned t = a % b; a = b; b = t; }
            unsigned step = 8 / a * (k + 1);
            for (unsigned n = 1; n <= 560; n++) {
                bool member = has && n >= lo && n <= hi && n % step == 0;
                if (rejects(n, k) == member) { bounds_ok = false; break; }
            }
        }
        CHECK(bounds_ok, "NBounds matches Params acceptance (k 0..70, n 1..560, exhaustive)");
    }

    // T2.3 corner-case matrix at (48,5): near-misses (F10), out-of-range
    // indices (F11), and per-round collision/ordering tampers. C++ mirror of
    // the Rust `rejection_path_matrix` / near-miss tests; verifiers return
    // bool, so only rejection (not the exact variant) is checked.
    {
        Params P(48, 5);
        Requihash eh(P);
        auto input = bytes("matrix");
        size_t cByte = P.CollisionByteLength();
        unsigned k = P.k;
        size_t init_size = size_t(1) << (P.CollisionBitLength() + 1);
        struct HR { std::vector<unsigned char> h; std::vector<eh_index> idx; };
        auto disjoint = [](const std::vector<eh_index>& a, const std::vector<eh_index>& b) {
            for (auto x : a) for (auto y : b) if (x == y) return false;
            return true;
        };

        bool did_run = false;
        for (uint32_t ni = 0; ni < 200 && !did_run; ni++) {
            std::vector<unsigned char> nonce(4);
            nonce[0]=ni&0xFF; nonce[1]=(ni>>8)&0xFF; nonce[2]=(ni>>16)&0xFF; nonce[3]=(ni>>24)&0xFF;
            auto base = eh.InitialiseState(input, nonce);
            auto sols = Solve(eh, base);
            if (sols.empty()) continue;
            auto sol = sols[0];
            did_run = true;

            auto rejects = [&](const std::vector<eh_index>& v) {
                return !IsValidSolution(eh, base, v) && !IsValidSolutionEarly(eh, base, v);
            };

            // wrong length: empty, truncated, extended
            bool wl = rejects({}) &&
                      rejects(std::vector<eh_index>(sol.begin(), sol.end() - 1));
            { auto t = sol; t.push_back(sol[0]); wl = wl && rejects(t); }
            CHECK(wl, "verifiers reject wrong-length inputs");

            // duplicate index
            { auto t = sol; t[1] = t[0]; CHECK(rejects(t), "verifiers reject duplicate index"); }

            // out-of-range index (F11): first non-leaf value and UINT32_MAX
            {
                auto t = sol; t[0] = (eh_index)init_size;
                auto u = sol; u[0] = (eh_index)0xFFFFFFFFu;
                CHECK(rejects(t) && rejects(u), "verifiers reject out-of-range index (F11)");
            }

            // ordering tamper at every round: swap the first two level-(r-1)
            // subtrees; collisions still hold, canonical ordering breaks at r
            {
                bool all = true;
                for (unsigned r = 1; r <= k; r++) {
                    size_t span = size_t(1) << (r - 1);
                    auto t = sol;
                    std::rotate(t.begin(), t.begin() + span, t.begin() + 2 * span);
                    all = all && rejects(t);
                }
                CHECK(all, "verifiers reject subtree-swap ordering tamper at every round");
            }

            // harvest per-round rows (round t rows span 2^t leaves)
            std::vector<std::vector<HR>> rounds;
            {
                std::vector<HR> r0(init_size);
                std::vector<unsigned char> hashOut(P.HashOutput());
                std::vector<unsigned char> expanded((k + 1) * cByte);
                for (eh_index l = 0; l < (eh_index)init_size; l++) {
                    eh.GenerateHash(base, l, hashOut.data());
                    eh.ExpandSlice(hashOut.data(), 0, expanded.data());
                    r0[l].h = expanded;
                    r0[l].idx = {l};
                }
                rounds.push_back(std::move(r0));
            }
            for (unsigned t = 1; t <= k; t++) {
                auto& prev = rounds.back();
                std::vector<size_t> order(prev.size());
                for (size_t i = 0; i < order.size(); i++) order[i] = i;
                std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                    return memcmp(prev[a].h.data(), prev[b].h.data(), cByte) < 0;
                });
                std::vector<HR> next;
                size_t i = 0;
                while (i + 1 < order.size()) {
                    size_t j = i + 1;
                    while (j < order.size() &&
                           memcmp(prev[order[j]].h.data(), prev[order[i]].h.data(), cByte) == 0)
                        j++;
                    for (size_t a = i; a < j; a++) {
                        for (size_t b = a + 1; b < j; b++) {
                            const HR& ra = prev[order[a]];
                            const HR& rb = prev[order[b]];
                            if (!disjoint(ra.idx, rb.idx)) continue;
                            HR m;
                            size_t remain = ra.h.size() - cByte;
                            m.h.resize(remain);
                            for (size_t x = 0; x < remain; x++)
                                m.h[x] = ra.h[cByte + x] ^ rb.h[cByte + x];
                            const HR& lo = ra.idx < rb.idx ? ra : rb;
                            const HR& hi = ra.idx < rb.idx ? rb : ra;
                            m.idx = lo.idx;
                            m.idx.insert(m.idx.end(), hi.idx.begin(), hi.idx.end());
                            next.push_back(std::move(m));
                        }
                    }
                    i = j;
                }
                rounds.push_back(std::move(next));
            }

            // near-miss (F10): final-round rows with nonzero remaining hash
            {
                size_t tested = 0; bool all = true;
                for (const auto& r : rounds[k]) {
                    bool zero = true;
                    for (auto c : r.h) if (c) { zero = false; break; }
                    if (zero) continue;
                    tested++;
                    all = all && rejects(r.idx);
                }
                CHECK(tested > 0 && all, "verifiers reject nonzero-root near-misses (F10)");
            }

            // collision tamper at every round: concat mutually-disjoint
            // round-(r-1) rows, first two differing in the leading segment
            {
                bool all = true;
                for (unsigned r = 1; r <= k; r++) {
                    const auto& pool = rounds[r - 1];
                    size_t m = size_t(1) << (k - r + 1);
                    std::vector<size_t> sel;
                    std::vector<eh_index> used;
                    for (size_t pi = 0; pi < pool.size() && sel.size() < m; pi++) {
                        if (!disjoint(pool[pi].idx, used)) continue;
                        if (sel.size() == 1 &&
                            memcmp(pool[pi].h.data(), pool[sel[0]].h.data(), cByte) == 0)
                            continue;
                        used.insert(used.end(), pool[pi].idx.begin(), pool[pi].idx.end());
                        sel.push_back(pi);
                    }
                    if (sel.size() != m) { all = false; break; }
                    std::vector<eh_index> v;
                    for (auto pi : sel)
                        v.insert(v.end(), pool[pi].idx.begin(), pool[pi].idx.end());
                    all = all && rejects(v);
                }
                CHECK(all, "verifiers reject collision tamper at every round");
            }
        }
        CHECK(did_run, "corner-case matrix found a solution to tamper with");
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
