// Differential test: re-solves every vector in ../../../vectors/ (the
// canonical cs/ port's committed KATs, generated from the Python
// reference -- reused here, not duplicated, since V1 targets the exact
// same (n,K,nonce) points) with this variant's solver and asserts exact
// index-vector equality plus self-verification. Same parser/harness
// shape as cs/tests/differential.cpp; only the solver type changes.
#include "klist_v5.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct ParsedVector {
    unsigned n;
    unsigned k;
    std::string nonce_hex;
    std::vector<std::vector<uint32_t>> solutions;
};

void expect(const std::string& s, size_t& pos, const char* lit) {
    while (pos < s.size() && isspace((unsigned char)s[pos])) ++pos;
    size_t len = strlen(lit);
    if (s.compare(pos, len, lit) != 0) {
        throw std::runtime_error("expected '" + std::string(lit) + "' at " + std::to_string(pos));
    }
    pos += len;
}

void skip_ws(const std::string& s, size_t& pos) {
    while (pos < s.size() && isspace((unsigned char)s[pos])) ++pos;
}

unsigned parse_uint(const std::string& s, size_t& pos) {
    skip_ws(s, pos);
    size_t start = pos;
    while (pos < s.size() && isdigit((unsigned char)s[pos])) ++pos;
    return (unsigned)std::stoul(s.substr(start, pos - start));
}

std::string parse_string(const std::string& s, size_t& pos) {
    expect(s, pos, "\"");
    size_t start = pos;
    while (pos < s.size() && s[pos] != '"') ++pos;
    std::string out = s.substr(start, pos - start);
    expect(s, pos, "\"");
    return out;
}

std::vector<uint32_t> parse_uint_array(const std::string& s, size_t& pos) {
    expect(s, pos, "[");
    std::vector<uint32_t> out;
    skip_ws(s, pos);
    if (s[pos] == ']') { ++pos; return out; }
    while (true) {
        out.push_back(parse_uint(s, pos));
        skip_ws(s, pos);
        if (s[pos] == ',') { ++pos; continue; }
        break;
    }
    expect(s, pos, "]");
    return out;
}

ParsedVector parse_vector_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::stringstream buf;
    buf << f.rdbuf();
    std::string s = buf.str();
    size_t pos = 0;

    ParsedVector v;
    expect(s, pos, "{");
    expect(s, pos, "\"n\"");
    expect(s, pos, ":");
    v.n = parse_uint(s, pos);
    skip_ws(s, pos);
    expect(s, pos, ",");
    expect(s, pos, "\"k\"");
    expect(s, pos, ":");
    v.k = parse_uint(s, pos);
    skip_ws(s, pos);
    expect(s, pos, ",");
    expect(s, pos, "\"nonce_hex\"");
    expect(s, pos, ":");
    v.nonce_hex = parse_string(s, pos);
    skip_ws(s, pos);
    expect(s, pos, ",");
    expect(s, pos, "\"solutions\"");
    expect(s, pos, ":");
    expect(s, pos, "[");
    skip_ws(s, pos);
    if (s[pos] != ']') {
        while (true) {
            v.solutions.push_back(parse_uint_array(s, pos));
            skip_ws(s, pos);
            if (s[pos] == ',') { ++pos; skip_ws(s, pos); continue; }
            break;
        }
    }
    expect(s, pos, "]");
    return v;
}

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> out(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = (uint8_t)strtoul(hex.substr(i * 2, 2).c_str(), nullptr, 16);
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <vectors-dir>\n", argv[0]);
        return 1;
    }
    std::string dir = argv[1];

    std::vector<std::string> files;
    DIR* d = opendir(dir.c_str());
    if (!d) {
        fprintf(stderr, "cannot open directory %s\n", dir.c_str());
        return 1;
    }
    while (struct dirent* entry = readdir(d)) {
        std::string name = entry->d_name;
        if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
            files.push_back(dir + "/" + name);
        }
    }
    closedir(d);
    std::sort(files.begin(), files.end());

    if (files.size() < 4) {
        fprintf(stderr, "expected at least 4 vector files in %s, found %zu\n", dir.c_str(), files.size());
        return 1;
    }

    int failures = 0;
    for (const auto& path : files) {
        ParsedVector v = parse_vector_file(path);
        printf("checking %s (n=%u, k=%u)...\n", path.c_str(), v.n, v.k);

        cs_v5::KListWagnerAlgorithmV5 solver(v.n, v.k, hex_to_bytes(v.nonce_hex));
        auto solutions = solver.solve();

        // Compare as a SET, not a sequence: this variant's full-radix
        // sorted-key merge enumerates colliding pairs in a different
        // order than the reference's hash-table-iteration order (V1/V2/
        // cs/'s own hash_merge port happens to preserve the reference's
        // order since it iterates L2 outer/L1-hash-probe inner exactly
        // as the Python does; V5's sort-by-key merge does not). Root-
        // level solutions are inherently an unordered set at any one
        // merge round -- Sequihash's own algorithm has no ordering
        // guarantee to preserve beyond "every valid index tuple is
        // found," so byte-exact SET equality is the correct oracle here,
        // not sequence equality.
        auto sorted_solutions = solutions;
        auto sorted_expected = v.solutions;
        std::sort(sorted_solutions.begin(), sorted_solutions.end());
        std::sort(sorted_expected.begin(), sorted_expected.end());
        if (sorted_solutions != sorted_expected) {
            fprintf(stderr, "  MISMATCH: got %zu solution(s), vector has %zu\n",
                    solutions.size(), v.solutions.size());
            failures++;
            continue;
        }
        if (!solver.verify(solutions)) {
            fprintf(stderr, "  self-verification FAILED\n");
            failures++;
            continue;
        }
        printf("  OK: %zu solution(s) match exactly, self-verified\n", solutions.size());
    }

    if (failures > 0) {
        fprintf(stderr, "%d file(s) FAILED\n", failures);
        return 1;
    }
    printf("All %zu vector files matched.\n", files.size());
    return 0;
}
