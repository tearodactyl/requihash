// Generate cross-validation vectors: mine a Requihash solution and emit JSON
// { n, k, input_hex, nonce_hex, minimal_hex, indices } for the Rust verifier.
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "requihash.h"
#include "solver.h"

using namespace requihash;

static std::string hex(const std::vector<unsigned char>& v) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (auto b : v) { s += d[b >> 4]; s += d[b & 0xF]; }
    return s;
}

static void emit(FILE* f, unsigned int n, unsigned int k, const std::string& path) {
    Params P(n, k);
    Requihash eh(P);
    std::vector<unsigned char> input;
    for (char c : std::string("requihash-xcheck")) input.push_back((unsigned char)c);
    for (uint32_t ni = 0; ni < 2000; ni++) {
        if (ni % 50 == 0) { printf("  (%u,%u) scanning nonce %u...\n", n, k, ni); fflush(stdout); }
        std::vector<unsigned char> nonce(4);
        nonce[0] = ni & 0xFF; nonce[1] = (ni >> 8) & 0xFF;
        nonce[2] = (ni >> 16) & 0xFF; nonce[3] = (ni >> 24) & 0xFF;
        auto base = eh.InitialiseState(input, nonce);
        auto sols = Solve(eh, base);
        for (auto& s : sols) {
            if (!IsValidSolution(eh, base, s)) continue;
            auto minimal = GetMinimalFromIndices(s, P.CollisionBitLength());
            FILE* out = fopen(path.c_str(), "w");
            fprintf(out, "{\n");
            fprintf(out, "  \"n\": %u,\n  \"k\": %u,\n", n, k);
            fprintf(out, "  \"input_hex\": \"%s\",\n", hex(input).c_str());
            fprintf(out, "  \"nonce_hex\": \"%s\",\n", hex(nonce).c_str());
            fprintf(out, "  \"minimal_hex\": \"%s\",\n", hex(minimal).c_str());
            fprintf(out, "  \"indices\": [");
            for (size_t i = 0; i < s.size(); i++)
                fprintf(out, "%s%u", i ? ", " : "", s[i]);
            fprintf(out, "]\n}\n");
            fclose(out);
            printf("wrote %s (n=%u k=%u nonce=%u, %zu-byte solution)\n", path.c_str(), n, k,
                   ni, minimal.size());
            return;
        }
    }
    printf("no solution found for (%u,%u) within budget\n", n, k);
}

int main(int argc, char** argv) {
    std::string dir = (argc > 1) ? argv[1] : "../vectors";
    emit(stdout, 48, 5, dir + "/vec_48_5.json");
    emit(stdout, 72, 5, dir + "/vec_72_5.json");
    return 0;
}
