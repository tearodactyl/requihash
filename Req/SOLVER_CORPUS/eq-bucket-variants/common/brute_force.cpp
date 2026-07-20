#include "brute_force.hpp"

namespace eq_common {

namespace {
struct Row {
    FixedUint value;
    std::vector<uint32_t> indices;
};
} // namespace

std::vector<std::vector<uint32_t>> solve_brute_force(const Params& p, const std::vector<uint8_t>& nonce) {
    uint64_t count = p.leaf_count();
    std::vector<Row> rows;
    rows.reserve(count);
    for (uint64_t j = 0; j < count; ++j) {
        rows.push_back(Row{compute_leaf(p, nonce, (uint32_t)j), {(uint32_t)j}});
    }

    for (unsigned round = 1; round <= p.k; ++round) {
        unsigned mask_bit = (round == p.k) ? p.ell * 2 : p.ell;
        std::vector<Row> next;
        // Full O(rows^2) pairwise scan, no bucketing whatsoever.
        for (size_t a = 0; a < rows.size(); ++a) {
            for (size_t b = a + 1; b < rows.size(); ++b) {
                if (rows[a].value.low_bits_key(mask_bit) != rows[b].value.low_bits_key(mask_bit)) continue;
                bool overlap = false;
                for (uint32_t xi : rows[a].indices) {
                    for (uint32_t xj : rows[b].indices) {
                        if (xi == xj) { overlap = true; break; }
                    }
                    if (overlap) break;
                }
                if (overlap) continue;
                Row out;
                out.value = (rows[a].value ^ rows[b].value).shr(mask_bit);
                bool a_first = rows[a].indices < rows[b].indices;
                const Row& first = a_first ? rows[a] : rows[b];
                const Row& second = a_first ? rows[b] : rows[a];
                out.indices = first.indices;
                out.indices.insert(out.indices.end(), second.indices.begin(), second.indices.end());
                next.push_back(std::move(out));
            }
        }
        rows = std::move(next);
        if (rows.empty()) break;
    }

    std::vector<std::vector<uint32_t>> solutions;
    for (const auto& r : rows) {
        if (r.value.is_zero() && r.indices.size() == (size_t(1) << p.k)) {
            solutions.push_back(r.indices);
        }
    }
    return solutions;
}

} // namespace eq_common
