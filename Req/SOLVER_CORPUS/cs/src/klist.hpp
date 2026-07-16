// Canonical C++ port of the 2025/1351 paper's own Python "Sequihash"
// k-list Wagner-algorithm reference solver
// (~/Work/ZK/ZKs/Generalized-Birthday-Problem/GBP-solver/k_list_algorithm.py,
// 273 lines). A faithful port of the algorithm only -- the reference
// mixes rich/tracemalloc profiling and console output directly into the
// algorithm class; none of that is ported (no stdio, no logging in this
// module).
//
// Two conventions differ from this project's own Rust/C++ code
// (Req/rust, Req/cpp) and are reproduced here exactly, not translated:
//
//   1. `k` is a LIST COUNT (a power of 2), matching the paper's own
//      (n, K=2^k) table -- NOT Req's tree-depth convention where k is
//      the exponent and 2^k is the *solution size*. Here, `k` in the
//      public API IS the solution size (equal to the paper's K).
//   2. Leaf encoding is an ASCII decimal string "i-j" (i = list index,
//      j = item index within the list) appended to the raw nonce bytes,
//      NOT Req's binary le32(i mod k) || le32(i div k) encoding. This
//      matches k_list_algorithm.py's compute_item:
//      `self.nonce + f"{i}-{j}".encode()`.
//
// See k_list_algorithm.py's own docstrings for the algorithm's meaning;
// comments below cite the corresponding Python method by name.

#ifndef CS_KLIST_HPP
#define CS_KLIST_HPP

#include <cstdint>
#include <vector>

namespace cs {

// One (hash_value, index_vector) pair carried through the merge tree.
// hash_value is the XOR-accumulator, truncated by mask_bit at each
// merge step (Python: `merge_x = (x1 ^ x2) >> mask_bit`). index_vector
// accumulates one index per leaf list consumed so far.
struct HashItem {
    // Big unsigned integer, up to n bits (n <= 256 per the class's own
    // practical range; stored as a byte string, most-significant byte
    // first, matching Python's int.from_bytes(..., 'big') exactly --
    // NOT a fixed-width machine integer, since n is caller-chosen and
    // can exceed 64 bits).
    std::vector<uint8_t> value;
    std::vector<uint32_t> index_vector;
};

// Faithful port of k_list_wagner_algorithm. Preserves the class's
// public method shape (`new`-equivalent constructor, `solve`) per the
// port task's requirement to keep it "recognizable against the Python
// original."
class KListWagnerAlgorithm {
public:
    // Matches __init__: n (hash output bits, multiple of 8), k (list
    // count, power of 2, this class's own convention -- see file header
    // point 1), nonce (16 raw bytes).
    KListWagnerAlgorithm(unsigned n, unsigned k, std::vector<uint8_t> nonce);

    // Matches solve(index_bit_length=None): no index-trimming trade-off.
    // Only the no-trade-off path is ported -- the paper's own trade-off
    // mode (index_bit_length set) is a separate, more involved feature
    // not required by any of this port's vectored KAT points.
    std::vector<std::vector<uint32_t>> solve() const;

    // Matches verify_results: recomputes each solution's XOR-accumulator
    // from scratch and checks it is exactly zero (all n bits).
    bool verify(const std::vector<std::vector<uint32_t>>& solutions) const;

    unsigned n() const { return n_; }
    unsigned k() const { return k_; }
    unsigned lgk() const { return lgk_; }
    unsigned ell() const { return ell_; }

    // Matches compute_item(i, j): hashes nonce + ascii("{i}-{j}") with
    // BLAKE2b, digest length n/8 bytes, returned as a big-endian byte
    // string (see HashItem::value).
    std::vector<uint8_t> compute_item(unsigned i, unsigned j) const;

private:
    unsigned n_;
    unsigned k_;
    unsigned lgk_;   // log2(k) -- the merge-tree depth
    unsigned ell_;   // n / (lgk + 1) -- bits collided per ordinary merge round
    std::vector<uint8_t> nonce_;
    unsigned hash_size_; // n / 8 bytes

    // Matches compute_hash_list_on_the_fly(i, None, None): the full,
    // untrimmed leaf list for list index i -- 2^ell items,
    // (hash_value, [j]) pairs for j in [0, 2^ell).
    std::vector<HashItem> compute_hash_list(unsigned i) const;

    // Matches hash_merge(L1, L2, mask_bit): O(|L1|+|L2|) hash-join on
    // the low mask_bit bits of each item's value, producing
    // (merged_value >> mask_bit, idx1 ++ idx2) for every colliding pair.
    static std::vector<HashItem> hash_merge(
        const std::vector<HashItem>& l1,
        const std::vector<HashItem>& l2,
        unsigned mask_bit);

    // Matches _solve(None, None, verbose): the stack-based post-order
    // binary merge tree over the k leaf lists.
    std::vector<std::vector<uint32_t>> solve_internal() const;
};

} // namespace cs

#endif // CS_KLIST_HPP
