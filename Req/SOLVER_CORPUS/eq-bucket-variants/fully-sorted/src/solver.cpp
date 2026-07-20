#include "solver.hpp"

#include <algorithm>
#include <stdexcept>

namespace eq_fully_sorted {

namespace {

struct Row {
    FixedUint value;
    std::vector<uint32_t> indices;
};

} // namespace

Solver::Solver(unsigned n, unsigned k, std::vector<uint8_t> nonce)
    : p_(n, k), nonce_(std::move(nonce)) {
    if (nonce_.size() != 16) throw std::invalid_argument("nonce should be 16 bytes");
}

std::vector<std::vector<uint32_t>> Solver::solve() const {
    uint64_t count = p_.leaf_count();
    std::vector<Row> rows;
    rows.reserve(count);
    for (uint64_t j = 0; j < count; ++j) {
        rows.push_back(Row{eq_common::compute_leaf(p_, nonce_, (uint32_t)j), {(uint32_t)j}});
    }

    for (unsigned round = 1; round <= p_.k; ++round) {
        unsigned mask_bit = (round == p_.k) ? p_.ell * 2 : p_.ell;

        // Bucket partition on a bounded key prefix (same bucket.rs/V2
        // shape: min(mask_bit, 16) leading bits), counting sort.
        unsigned bucket_bits = std::min(mask_bit, 16u);
        size_t nbuckets = size_t(1) << bucket_bits;
        uint64_t bucket_mask = (bucket_bits == 64) ? ~0ull : ((uint64_t(1) << bucket_bits) - 1);
        auto bucket_of = [&](const FixedUint& v) { return v.low_bits_key(mask_bit) & bucket_mask; };

        std::vector<uint32_t> counts(nbuckets + 1, 0);
        for (const auto& r : rows) counts[bucket_of(r.value) + 1]++;
        for (size_t b = 0; b < nbuckets; ++b) counts[b + 1] += counts[b];
        std::vector<uint32_t> order(rows.size());
        std::vector<uint32_t> cursor(counts.begin(), counts.end());
        for (uint32_t i = 0; i < rows.size(); ++i) {
            size_t b = bucket_of(rows[i].value);
            order[cursor[b]++] = i;
        }

        std::vector<Row> next;
        for (size_t b = 0; b < nbuckets; ++b) {
            uint32_t lo = counts[b], hi = counts[b + 1];
            if (hi - lo < 2) continue;

            // FULL COMPARISON SORT of this bucket's rows by the exact
            // mask_bit key (not just the bucket-prefix bits) -- the
            // deliberate difference from bucket.rs's linear exact-match
            // scan. std::sort, O(bucket_size * log(bucket_size)).
            std::vector<uint32_t> bucket_rows(order.begin() + lo, order.begin() + hi);
            std::sort(bucket_rows.begin(), bucket_rows.end(), [&](uint32_t a, uint32_t c) {
                return rows[a].value.low_bits_key(mask_bit) < rows[c].value.low_bits_key(mask_bit);
            });

            // Scan adjacent equal-key runs in the now fully-sorted order.
            size_t i = 0;
            while (i + 1 < bucket_rows.size()) {
                size_t j = i + 1;
                uint64_t key_i = rows[bucket_rows[i]].value.low_bits_key(mask_bit);
                while (j < bucket_rows.size() && rows[bucket_rows[j]].value.low_bits_key(mask_bit) == key_i) ++j;
                for (size_t a = i; a < j; ++a) {
                    for (size_t c = a + 1; c < j; ++c) {
                        const Row& ra = rows[bucket_rows[a]];
                        const Row& rc = rows[bucket_rows[c]];
                        // Distinct-index check (Wagner's own requirement).
                        bool overlap = false;
                        for (uint32_t xi : ra.indices) {
                            for (uint32_t xj : rc.indices) {
                                if (xi == xj) { overlap = true; break; }
                            }
                            if (overlap) break;
                        }
                        if (overlap) continue;
                        Row out;
                        out.value = (ra.value ^ rc.value).shr(mask_bit);
                        bool ra_first = ra.indices < rc.indices;
                        const Row& first = ra_first ? ra : rc;
                        const Row& second = ra_first ? rc : ra;
                        out.indices = first.indices;
                        out.indices.insert(out.indices.end(), second.indices.begin(), second.indices.end());
                        next.push_back(std::move(out));
                    }
                }
                i = j;
            }
        }
        rows = std::move(next);
        if (rows.empty()) break;
    }

    std::vector<std::vector<uint32_t>> solutions;
    for (const auto& r : rows) {
        if (r.value.is_zero() && r.indices.size() == (size_t(1) << p_.k)) {
            solutions.push_back(r.indices);
        }
    }
    return solutions;
}

} // namespace eq_fully_sorted
