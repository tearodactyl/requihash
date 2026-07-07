// Requihash basic solver + verifier. Wagner's binary-tree collision search over
// the regularity-constrained leaf generator in requihash.h. This is the BasicSolve
// analogue of zcash Equihash: correctness-oriented, not memory-optimised.
#ifndef REQ_SOLVER_H
#define REQ_SOLVER_H

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

#include "requihash.h"

namespace requihash {

// A working row during the merge: `hash` holds the remaining (un-collided) bytes,
// `indices` holds the leaf indices in canonical (sorted-subtree) order.
struct WRow {
    std::vector<unsigned char> hash;
    std::vector<eh_index> indices;
};

inline bool distinct(const std::vector<eh_index>& a, const std::vector<eh_index>& b) {
    for (auto x : a)
        for (auto y : b)
            if (x == y) return false;
    return true;
}

// Compare two index vectors lexicographically (canonical ordering tie-break).
inline bool indicesBefore(const std::vector<eh_index>& a, const std::vector<eh_index>& b) {
    return a < b;
}

// Build the full leaf list, run k rounds of collide-on-ell-bits merges, return all
// solutions as index vectors (each of length 2^k). Basic (unoptimised) variant.
//
// Memory discipline: each row stores only the *remaining* (not-yet-collided) hash
// bytes. Round r collides on the first cByte bytes of every row, then trims them,
// so rows shrink from (k+1)*cByte down to cByte over the k rounds. This keeps the
// working set bounded at roughly the initial list size (2^(ell+1)) rather than
// growing with the carried hash width.
inline std::vector<std::vector<eh_index>> Solve(const Requihash& eh,
                                                const reqb2::State& base) {
    const Params& P = eh.params();
    size_t cByte = P.CollisionByteLength();
    size_t hashLen = (P.k + 1) * cByte;
    size_t init_size = size_t(1) << (P.CollisionBitLength() + 1);

    std::vector<WRow> X;
    X.reserve(init_size);

    // Generate leaves. Each leaf is keyed by its position (list-class = leaf mod k),
    // so leaves are generated independently to keep the regularity exact.
    std::vector<unsigned char> hashOut(P.HashOutput());
    std::vector<unsigned char> expanded(hashLen);
    for (eh_index leaf = 0; leaf < (eh_index)init_size; leaf++) {
        eh.GenerateHash(base, leaf, hashOut.data());
        eh.ExpandSlice(hashOut.data(), 0, expanded.data());
        WRow r;
        r.hash.assign(expanded.begin(), expanded.end());  // full (k+1)*cByte
        r.indices.push_back(leaf);
        X.push_back(std::move(r));
    }

    // k rounds of Wagner merging. Each row's `hash` always begins at the current
    // round's collision segment (we trim the front cByte after each round).
    for (unsigned int round = 1; round <= P.k; round++) {
        std::stable_sort(X.begin(), X.end(), [&](const WRow& a, const WRow& b) {
            return memcmp(a.hash.data(), b.hash.data(), cByte) < 0;
        });
        std::vector<WRow> Xc;
        size_t i = 0;
        while (i + 1 < X.size()) {
            size_t j = i + 1;
            while (j < X.size() &&
                   memcmp(X[i].hash.data(), X[j].hash.data(), cByte) == 0)
                j++;
            for (size_t a = i; a < j; a++) {
                for (size_t b = a + 1; b < j; b++) {
                    if (!distinct(X[a].indices, X[b].indices)) continue;
                    size_t remain = X[a].hash.size();
                    WRow merged;
                    // XOR remaining bytes, then drop the leading cByte (now zero).
                    merged.hash.resize(remain, 0);
                    for (size_t t = 0; t < remain; t++)
                        merged.hash[t] = X[a].hash[t] ^ X[b].hash[t];
                    merged.hash.erase(merged.hash.begin(), merged.hash.begin() + cByte);
                    if (indicesBefore(X[a].indices, X[b].indices)) {
                        merged.indices = X[a].indices;
                        merged.indices.insert(merged.indices.end(), X[b].indices.begin(),
                                              X[b].indices.end());
                    } else {
                        merged.indices = X[b].indices;
                        merged.indices.insert(merged.indices.end(), X[a].indices.begin(),
                                              X[a].indices.end());
                    }
                    Xc.push_back(std::move(merged));
                }
            }
            i = j;
        }
        X = std::move(Xc);
        if (X.empty()) break;
    }

    // Solutions: rows whose full remaining hash is zero and indices all distinct.
    std::vector<std::vector<eh_index>> sols;
    for (auto& r : X) {
        bool zero = true;
        for (auto c : r.hash)
            if (c != 0) { zero = false; break; }
        if (!zero) continue;
        std::set<eh_index> uniq(r.indices.begin(), r.indices.end());
        if (uniq.size() != r.indices.size()) continue;
        sols.push_back(r.indices);
    }
    return sols;
}

// Verifier: given a full 2^k index vector, recompute the tree and check
//   (a) collision structure round by round,
//   (b) canonical ordering of subtrees,
//   (c) distinct indices,
//   (d) XOR-to-zero at the root.
// The regularity is enforced implicitly: GenerateHash keys each leaf by its
// position, so an index vector that would be valid for single-list Equihash
// (wrong leaf->class mapping) fails the collision checks here.
inline bool IsValidSolution(const Requihash& eh, const reqb2::State& base,
                            const std::vector<eh_index>& indices) {
    const Params& P = eh.params();
    size_t expected = size_t(1) << P.k;
    if (indices.size() != expected) return false;
    size_t cByte = P.CollisionByteLength();
    size_t hashLen = (P.k + 1) * cByte;

    // distinct check
    {
        std::set<eh_index> uniq(indices.begin(), indices.end());
        if (uniq.size() != indices.size()) return false;
    }

    // rebuild leaf rows in given order
    std::vector<WRow> X(indices.size());
    std::vector<unsigned char> hashOut(P.HashOutput());
    std::vector<unsigned char> expanded(hashLen);
    for (size_t i = 0; i < indices.size(); i++) {
        eh.GenerateHash(base, indices[i], hashOut.data());
        eh.ExpandSlice(hashOut.data(), 0, expanded.data());
        X[i].hash.assign(expanded.begin(), expanded.end());
        X[i].indices.push_back(indices[i]);
    }

    // fold k rounds, checking collisions and ordering
    for (unsigned int round = 1; round <= P.k; round++) {
        size_t off = (round - 1) * cByte;
        std::vector<WRow> Xc;
        for (size_t i = 0; i < X.size(); i += 2) {
            const WRow& a = X[i];
            const WRow& b = X[i + 1];
            // collide on this round's segment
            if (memcmp(a.hash.data() + off, b.hash.data() + off, cByte) != 0) return false;
            // canonical ordering: left subtree indices < right subtree indices
            if (!indicesBefore(a.indices, b.indices)) return false;
            WRow m;
            m.hash.resize(hashLen, 0);
            for (size_t t = 0; t < hashLen; t++) m.hash[t] = a.hash[t] ^ b.hash[t];
            m.indices = a.indices;
            m.indices.insert(m.indices.end(), b.indices.begin(), b.indices.end());
            Xc.push_back(std::move(m));
        }
        X = std::move(Xc);
    }
    if (X.size() != 1) return false;
    for (auto c : X[0].hash)
        if (c != 0) return false;
    return true;
}

// ---- Arena solver: flat struct-of-arrays, permutation sort. Same algorithm and
// output as Solve(), but no per-row std::vector allocation. Port of the Rust
// solve_arena; the profile showed per-row allocation is 59% of solve time. ----
inline std::vector<std::vector<eh_index>> SolveArena(const Requihash& eh,
                                                     const reqb2::State& base) {
    const Params& P = eh.params();
    size_t cByte = P.CollisionByteLength();
    size_t full = (P.k + 1) * cByte;
    size_t init_size = size_t(1) << (P.CollisionBitLength() + 1);

    size_t hstride = full, icount = 1, nrows = init_size;
    std::vector<unsigned char> hashes(nrows * hstride);
    std::vector<eh_index> idxs(nrows * icount);
    {
        std::vector<unsigned char> hashOut(P.HashOutput());
        std::vector<unsigned char> expanded(full);
        for (eh_index leaf = 0; leaf < (eh_index)nrows; leaf++) {
            eh.GenerateHash(base, leaf, hashOut.data());
            eh.ExpandSlice(hashOut.data(), 0, expanded.data());
            memcpy(hashes.data() + (size_t)leaf * hstride, expanded.data(), full);
            idxs[leaf] = leaf;
        }
    }

    for (unsigned int round = 1; round <= P.k; round++) {
        std::vector<uint32_t> order(nrows);
        for (uint32_t i = 0; i < nrows; i++) order[i] = i;
        std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
            return memcmp(hashes.data() + (size_t)a * hstride,
                          hashes.data() + (size_t)b * hstride, cByte) < 0;
        });

        size_t new_hstride = hstride - cByte, new_icount = icount * 2;
        std::vector<unsigned char> out_hashes;
        std::vector<eh_index> out_idxs;

        size_t i = 0;
        while (i + 1 < order.size()) {
            size_t ri = order[i];
            const unsigned char* key = hashes.data() + ri * hstride;
            size_t j = i + 1;
            while (j < order.size() &&
                   memcmp(hashes.data() + (size_t)order[j] * hstride, key, cByte) == 0)
                j++;
            for (size_t a = i; a < j; a++) {
                size_t ra = order[a];
                for (size_t b = a + 1; b < j; b++) {
                    size_t rb = order[b];
                    const eh_index* ia = idxs.data() + ra * icount;
                    const eh_index* ib = idxs.data() + rb * icount;
                    bool distinct = true;
                    for (size_t x = 0; x < icount && distinct; x++)
                        for (size_t y = 0; y < icount; y++)
                            if (ia[x] == ib[y]) { distinct = false; break; }
                    if (!distinct) continue;
                    size_t base_off = out_hashes.size();
                    out_hashes.resize(base_off + new_hstride, 0);
                    const unsigned char* ha = hashes.data() + ra * hstride + cByte;
                    const unsigned char* hb = hashes.data() + rb * hstride + cByte;
                    for (size_t t = 0; t < new_hstride; t++)
                        out_hashes[base_off + t] = ha[t] ^ hb[t];
                    bool aFirst = std::lexicographical_compare(ia, ia + icount, ib, ib + icount);
                    const eh_index* lo = aFirst ? ia : ib;
                    const eh_index* hi = aFirst ? ib : ia;
                    out_idxs.insert(out_idxs.end(), lo, lo + icount);
                    out_idxs.insert(out_idxs.end(), hi, hi + icount);
                }
            }
            i = j;
        }
        hashes.swap(out_hashes);
        idxs.swap(out_idxs);
        hstride = new_hstride;
        icount = new_icount;
        nrows = (hstride == 0) ? (idxs.size() / new_icount) : (hashes.size() / hstride);
        if (nrows == 0) break;
    }

    std::vector<std::vector<eh_index>> sols;
    for (size_t r = 0; r < nrows; r++) {
        bool zero = true;
        if (hstride > 0)
            for (size_t t = 0; t < hstride; t++)
                if (hashes[r * hstride + t]) { zero = false; break; }
        if (!zero) continue;
        std::vector<eh_index> idx(idxs.begin() + r * icount, idxs.begin() + (r + 1) * icount);
        std::set<eh_index> uniq(idx.begin(), idx.end());
        if (uniq.size() == idx.size()) sols.push_back(std::move(idx));
    }
    return sols;
}

// Early-reject verifier: fused fold, rejects on first bad collision/ordering,
// tracks index sub-ranges instead of building index vectors. Port of the Rust
// verify::early.
inline bool IsValidSolutionEarly(const Requihash& eh, const reqb2::State& base,
                                 const std::vector<eh_index>& indices) {
    const Params& P = eh.params();
    size_t expected = size_t(1) << P.k;
    if (indices.size() != expected) return false;
    size_t cByte = P.CollisionByteLength();
    size_t full = (P.k + 1) * cByte;
    {
        std::set<eh_index> uniq(indices.begin(), indices.end());
        if (uniq.size() != indices.size()) return false;
    }
    std::vector<std::vector<unsigned char>> level(indices.size());
    {
        std::vector<unsigned char> hashOut(P.HashOutput()), expanded(full);
        for (size_t i = 0; i < indices.size(); i++) {
            eh.GenerateHash(base, indices[i], hashOut.data());
            eh.ExpandSlice(hashOut.data(), 0, expanded.data());
            level[i].assign(expanded.begin(), expanded.end());
        }
    }
    size_t span = 1;
    for (unsigned int round = 1; round <= P.k; round++) {
        size_t off = (round - 1) * cByte;
        std::vector<std::vector<unsigned char>> next;
        next.reserve(level.size() / 2);
        for (size_t a = 0; a < level.size(); a += 2) {
            size_t b = a + 1;
            if (memcmp(level[a].data() + off, level[b].data() + off, cByte) != 0) return false;
            size_t lo = a * span, mid = lo + span, hi = mid + span;
            if (!std::lexicographical_compare(indices.begin() + lo, indices.begin() + mid,
                                              indices.begin() + mid, indices.begin() + hi))
                return false;
            std::vector<unsigned char> m(full);
            for (size_t t = 0; t < full; t++) m[t] = level[a][t] ^ level[b][t];
            next.push_back(std::move(m));
        }
        level.swap(next);
        span *= 2;
    }
    if (level.size() != 1) return false;
    for (size_t t = 0; t < P.k * cByte; t++)
        if (level[0][t]) return false;
    return true;
}

}  // namespace requihash

#endif
