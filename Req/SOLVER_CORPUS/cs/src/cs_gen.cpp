// Driver: `cs_gen <n> <k> <nonce_hex>` runs KListWagnerAlgorithm::solve()
// and prints one JSON line `{"n":...,"k":...,"nonce_hex":"...","solutions":[[...],...]}`,
// matching the schema of the vectors generated from the Python
// reference (vectors/*.json) so a diff/comparison tool can treat both
// uniformly. Harness code -- no algorithm logic lives here.
#include "klist.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace cs;

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> out(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = (uint8_t)strtoul(hex.substr(i * 2, 2).c_str(), nullptr, 16);
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <n> <k> <nonce_hex (32 hex chars = 16 bytes)>\n", argv[0]);
        return 1;
    }
    unsigned n = (unsigned)atoi(argv[1]);
    unsigned k = (unsigned)atoi(argv[2]);
    std::vector<uint8_t> nonce = hex_to_bytes(argv[3]);

    KListWagnerAlgorithm solver(n, k, nonce);
    auto solutions = solver.solve();
    bool ok = solver.verify(solutions);

    printf("{\"n\":%u,\"k\":%u,\"nonce_hex\":\"%s\",\"solutions\":[", n, k, argv[3]);
    for (size_t s = 0; s < solutions.size(); ++s) {
        if (s) printf(",");
        printf("[");
        for (size_t idx = 0; idx < solutions[s].size(); ++idx) {
            if (idx) printf(",");
            printf("%u", solutions[s][idx]);
        }
        printf("]");
    }
    printf("],\"verified\":%s}\n", ok ? "true" : "false");
    return 0;
}
