// C++ Requihash benchmark, mirroring rust/src/bin/req_bench.rs exactly so the two
// backends can be compared on identical parameters and the same three hot paths:
//   [1] leaf hashing throughput   [2] solve gen/merge split   [3] verify throughput
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "requihash.h"
#include "solver.h"

using namespace requihash;
using namespace std::chrono;

static std::vector<unsigned char> bytes(const std::string& s) {
    return std::vector<unsigned char>(s.begin(), s.end());
}

static uint64_t median(std::vector<uint64_t> xs) {
    std::sort(xs.begin(), xs.end());
    return xs[xs.size() / 2];
}

// [1] leaf hashing throughput: generate all 2^(ell+1) leaves, no merge.
static void bench_leaf_hash(unsigned int n, unsigned int k) {
    Params P(n, k);
    Requihash eh(P);
    auto base = eh.InitialiseState(bytes("bench-input"), std::vector<unsigned char>(4, 0));
    size_t hashLen = (P.k + 1) * P.CollisionByteLength();
    size_t init_size = size_t(1) << (P.CollisionBitLength() + 1);

    std::vector<uint64_t> samples;
    for (int rep = 0; rep < 5; rep++) {
        auto t0 = high_resolution_clock::now();
        std::vector<unsigned char> hashOut(P.HashOutput()), expanded(hashLen);
        volatile unsigned char sink = 0;
        for (eh_index leaf = 0; leaf < (eh_index)init_size; leaf++) {
            eh.GenerateHash(base, leaf, hashOut.data());
            eh.ExpandSlice(hashOut.data(), 0, expanded.data());
            sink ^= expanded[0];
        }
        (void)sink;
        auto t1 = high_resolution_clock::now();
        samples.push_back(duration_cast<nanoseconds>(t1 - t0).count());
    }
    uint64_t ns = median(samples);
    double per_leaf = (double)ns / init_size;
    double rate = init_size / ((double)ns / 1e9);
    printf("  leaf-hash (%u,%u): %zu leaves in %.2f ms  |  %.1f ns/leaf  |  %.2f M leaves/s\n",
           n, k, init_size, ns / 1e6, per_leaf, rate / 1e6);
}

// [2] full solve with gen/merge split. We time generation and merge separately by
// replicating Solve()'s structure here (same allocation pattern as solver.h).
static void bench_solve(unsigned int n, unsigned int k) {
    Params P(n, k);
    Requihash eh(P);
    size_t cByte = P.CollisionByteLength();
    size_t hashLen = (P.k + 1) * cByte;
    size_t init_size = size_t(1) << (P.CollisionBitLength() + 1);

    for (uint32_t ni = 0; ni <= 4000; ni++) {
        std::vector<unsigned char> nonce(4);
        nonce[0] = ni & 0xFF; nonce[1] = (ni >> 8) & 0xFF;
        nonce[2] = (ni >> 16) & 0xFF; nonce[3] = (ni >> 24) & 0xFF;
        auto base = eh.InitialiseState(bytes("bench-input"), nonce);

        auto tg = high_resolution_clock::now();
        std::vector<WRow> X;
        X.reserve(init_size);
        std::vector<unsigned char> hashOut(P.HashOutput()), expanded(hashLen);
        for (eh_index leaf = 0; leaf < (eh_index)init_size; leaf++) {
            eh.GenerateHash(base, leaf, hashOut.data());
            eh.ExpandSlice(hashOut.data(), 0, expanded.data());
            WRow r;
            r.hash.assign(expanded.begin(), expanded.end());
            r.indices.push_back(leaf);
            X.push_back(std::move(r));
        }
        uint64_t gen_ns = duration_cast<nanoseconds>(high_resolution_clock::now() - tg).count();

        auto tm = high_resolution_clock::now();
        std::vector<size_t> sizes{X.size()};
        for (unsigned int round = 1; round <= P.k; round++) {
            std::stable_sort(X.begin(), X.end(), [&](const WRow& a, const WRow& b) {
                return memcmp(a.hash.data(), b.hash.data(), cByte) < 0;
            });
            std::vector<WRow> Xc;
            size_t i = 0;
            while (i + 1 < X.size()) {
                size_t j = i + 1;
                while (j < X.size() && memcmp(X[i].hash.data(), X[j].hash.data(), cByte) == 0) j++;
                for (size_t a = i; a < j; a++) {
                    for (size_t b = a + 1; b < j; b++) {
                        bool ok = true;
                        for (auto x : X[a].indices) { for (auto y : X[b].indices) if (x == y) { ok = false; break; } if (!ok) break; }
                        if (!ok) continue;
                        size_t remain = X[a].hash.size();
                        WRow m; m.hash.resize(remain, 0);
                        for (size_t t = 0; t < remain; t++) m.hash[t] = X[a].hash[t] ^ X[b].hash[t];
                        m.hash.erase(m.hash.begin(), m.hash.begin() + cByte);
                        if (X[a].indices < X[b].indices) {
                            m.indices = X[a].indices;
                            m.indices.insert(m.indices.end(), X[b].indices.begin(), X[b].indices.end());
                        } else {
                            m.indices = X[b].indices;
                            m.indices.insert(m.indices.end(), X[a].indices.begin(), X[a].indices.end());
                        }
                        Xc.push_back(std::move(m));
                    }
                }
                i = j;
            }
            X = std::move(Xc);
            sizes.push_back(X.size());
            if (X.empty()) break;
        }
        uint64_t merge_ns = duration_cast<nanoseconds>(high_resolution_clock::now() - tm).count();

        size_t nsol = 0;
        for (auto& r : X) { bool z = true; for (auto c : r.hash) if (c) { z = false; break; } if (z) nsol++; }
        if (nsol == 0) continue;

        uint64_t total = gen_ns + merge_ns;
        printf("  solve (%u,%u) nonce=%u: %.2f ms total  [gen %.1f%% / merge %.1f%%]  %zu sols\n",
               n, k, ni, total / 1e6, 100.0 * gen_ns / total, 100.0 * merge_ns / total, nsol);
        printf("    round list sizes:");
        for (auto s : sizes) printf(" %zu", s);
        printf("\n");
        return;
    }
    printf("  solve (%u,%u): no solution within nonce budget\n", n, k);
}

// [3] verifier throughput.
static void bench_verify(unsigned int n, unsigned int k) {
    Params P(n, k);
    Requihash eh(P);
    std::vector<eh_index> sol;
    reqb2::State base = eh.InitialiseState(bytes("bench-input"), std::vector<unsigned char>(4, 0));
    uint32_t ni = 0;
    for (; ni <= 4000; ni++) {
        std::vector<unsigned char> nonce(4);
        nonce[0] = ni & 0xFF; nonce[1] = (ni >> 8) & 0xFF;
        nonce[2] = (ni >> 16) & 0xFF; nonce[3] = (ni >> 24) & 0xFF;
        base = eh.InitialiseState(bytes("bench-input"), nonce);
        auto sols = Solve(eh, base);
        if (!sols.empty()) { sol = sols[0]; break; }
    }
    if (sol.empty()) { printf("  verify (%u,%u): no solution\n", n, k); return; }

    const uint32_t iters = 2000;
    auto t0 = high_resolution_clock::now();
    volatile bool sink = false;
    for (uint32_t r = 0; r < iters; r++) sink ^= IsValidSolution(eh, base, sol);
    (void)sink;
    uint64_t ns = duration_cast<nanoseconds>(high_resolution_clock::now() - t0).count();
    double per = (double)ns / iters;
    printf("  verify (%u,%u): %.1f us/verify  |  %.0f verifies/s  (%zu leaves/verify)\n",
           n, k, per / 1e3, 1e9 / per, size_t(1) << k);
}

// [6] all solver backends: reference vs arena, median ms.
static void bench_all_solvers(unsigned int n, unsigned int k) {
    Params P(n, k);
    Requihash eh(P);
    uint32_t nonce = 0;
    reqb2::State base = eh.InitialiseState(bytes("bench-input"), std::vector<unsigned char>(4, 0));
    for (; nonce <= 4000; nonce++) {
        std::vector<unsigned char> nn(4);
        nn[0]=nonce&0xFF; nn[1]=(nonce>>8)&0xFF; nn[2]=(nonce>>16)&0xFF; nn[3]=(nonce>>24)&0xFF;
        base = eh.InitialiseState(bytes("bench-input"), nn);
        if (!Solve(eh, base).empty()) break;
    }
    int reps = n >= 96 ? 3 : 9;
    auto timed = [&](auto fn) {
        std::vector<uint64_t> s;
        for (int r = 0; r < reps; r++) {
            auto t = high_resolution_clock::now();
            auto v = fn(); (void)v;
            s.push_back(duration_cast<nanoseconds>(high_resolution_clock::now() - t).count());
        }
        return median(s) / 1e6;
    };
    double ref = timed([&]{ return Solve(eh, base); });
    double are = timed([&]{ return SolveArena(eh, base); });
    printf("  (%u,%u):  solve-reference=%.1fms  solve-arena=%.1fms\n", n, k, ref, are);
}

// [7] all verifier backends, us/verify.
static void bench_all_verifiers(unsigned int n, unsigned int k) {
    Params P(n, k);
    Requihash eh(P);
    uint32_t nonce = 0;
    reqb2::State base = eh.InitialiseState(bytes("bench-input"), std::vector<unsigned char>(4, 0));
    std::vector<eh_index> sol;
    for (; nonce <= 4000; nonce++) {
        std::vector<unsigned char> nn(4);
        nn[0]=nonce&0xFF; nn[1]=(nonce>>8)&0xFF; nn[2]=(nonce>>16)&0xFF; nn[3]=(nonce>>24)&0xFF;
        base = eh.InitialiseState(bytes("bench-input"), nn);
        auto s = Solve(eh, base);
        if (!s.empty()) { sol = s[0]; break; }
    }
    const uint32_t iters = 3000;
    auto timed = [&](auto fn) {
        auto t = high_resolution_clock::now();
        volatile bool sink = false;
        for (uint32_t r = 0; r < iters; r++) sink ^= fn();
        (void)sink;
        return duration_cast<nanoseconds>(high_resolution_clock::now() - t).count() / (double)iters;
    };
    double ref = timed([&]{ return IsValidSolution(eh, base, sol); });
    double early = timed([&]{ return IsValidSolutionEarly(eh, base, sol); });
    printf("  (%u,%u):  verify-reference=%.1fus  verify-early=%.1fus\n", n, k, ref/1e3, early/1e3);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("== Requihash benchmark (C++ multi-backend) ==\n");
    const std::pair<unsigned, unsigned> params[] = {{48, 5}, {72, 5}, {96, 5}};

    printf("\n[1] leaf hashing throughput (13-24%% of solve; see [2]):\n");
    for (auto [n, k] : params) bench_leaf_hash(n, k);

    printf("\n[2] full solve, gen vs merge split:\n");
    for (auto [n, k] : params) bench_solve(n, k);

    printf("\n[3] verifier throughput (consensus-critical latency):\n");
    for (auto [n, k] : params) bench_verify(n, k);

    printf("\n[6] SOLVER BACKENDS (seam B), median ms:\n");
    for (auto [n, k] : params) bench_all_solvers(n, k);

    printf("\n[7] VERIFIER BACKENDS, us/verify:\n");
    for (auto [n, k] : params) bench_all_verifiers(n, k);
    return 0;
}
