// Single-attempt driver: rk_single_bench <n> <k> <seed> <nonce> [--reps N]
// [--no-save] runs InitializeMemory+FillMemory+ResolveCollisions(xk)
// repeatedly (default 5 reps, Req/BENCH.md S2's floor; no FindProof
// multi-nonce retry search) and reports min/median/MAD wall time + a
// process-level peak-RSS reading, as one JSON line matching
// reqbench::run_record::RunRecord's exact field set/order/types (Rust
// crate, Req/SOLVER_CORPUS/reqbench/src/run_record.rs -- see that file's
// own doc comment: this is the contract a C++ driver's printf must match
// since there is no shared C++ library across SOLVER_CORPUS ports).
//
// Every invocation writes a NEW file to
// Req/SOLVER_CORPUS/rk/original/runs/<timestamp>.jsonl (this binary's own
// runs/ subdirectory, resolved relative to argv[0]'s directory so it
// works regardless of caller cwd) -- never appends to an existing file,
// never touches the Rust sibling's own rk/runs/ directory. No
// implementation ever writes another implementation's file (see
// run_record.rs's module doc comment for why). Pass --no-save to print
// only.
//
// This file is a NEW harness. pow.h needed exactly one addition to
// support it -- a public Equihash::SetNonce(Nonce) setter, since `nonce`
// has no public accessor and (being private by C++'s class-default rule,
// not an explicit `private:` keyword) is not reachable via the usual
// `#define private public` trick either. See pow.h's own dated comment
// at the SetNonce declaration for the exact justification. pow.cc (the
// actual algorithm) is untouched.
#include "pow.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <fstream>
#include <sstream>
#endif

using namespace _POW;

// Peak RSS in bytes, this process, read at call time. macOS: Mach task
// info (mirrors what /usr/bin/time -l's "maximum resident set size"
// reports). Linux: /proc/self/status VmHWM (kernel-tracked high-water
// mark directly, no read-time race the way a repeated poll would have).
static long peak_rss_bytes() {
#if defined(__APPLE__)
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        return (long)info.resident_size_max;
    }
    return -1;
#elif defined(__linux__)
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmHWM:", 0) == 0) {
            std::istringstream iss(line.substr(6));
            long kb = 0;
            iss >> kb;
            return kb * 1024;
        }
    }
    return -1;
#else
    return -1;
#endif
}

// Mirrors reqbench::provenance::run_git -- shells out, never throws;
// missing git/not-a-repo becomes "unknown"/false rather than aborting.
static std::string run_git(const char* args) {
    std::string cmd = std::string("git ") + args + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    std::string out;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
    pclose(pipe);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

// Matches reqbench::run_record::run_filename exactly:
// "<n><k>_YYYYMMDDTHHMMSSZ.jsonl", UTC, dash-free timestamp portion (see
// that function's own doc comment for why the point prefix is required,
// not cosmetic -- a same-second, different-point collision was caught in
// practice during this fix's own validation run).
static std::string run_filename(unsigned n, unsigned k) {
    std::time_t t = std::time(nullptr);
    std::tm utc;
    gmtime_r(&t, &utc);
    char buf[48];
    snprintf(buf, sizeof(buf), "%u%u_%04d%02d%02dT%02d%02d%02dZ.jsonl",
        n, k,
        utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
        utc.tm_hour, utc.tm_min, utc.tm_sec);
    return buf;
}

// Directory this executable lives in, so runs/ resolves next to the
// binary regardless of the caller's cwd -- argv[0] may be relative
// (e.g. "./rk_single_bench" from within build/), so this only strips the
// trailing filename, it doesn't resolve an absolute path; run from the
// project's own build/ directory (as this port's other tools are) and it
// lands at build/../runs/ = original/runs/, matching the Rust sibling's
// CARGO_MANIFEST_DIR-relative rk/runs/ placement (one level up from
// each's own build/target directory).
// Pre-flight (n,k) check -- matches rk/src/lib.rs::check_nk exactly (see
// that function's own doc comment for the rule origins and the
// n%8==0-was-wrong correction this mirrors). Two rules only:
//   1. k >= 1
//   2. n % (k+1) == 0        -- ell=n/(k+1) must be a whole number
//   3. n/(k+1) <= 32         -- original pow.h's own MAX_N=32-byte assumption
// Returns empty string if valid, else a message naming the failed rule.
static std::string check_nk(unsigned n, unsigned k) {
    if (k < 1) return "k must be >= 1";
    if (n % (k + 1) != 0) {
        return "n must be divisible by k+1=" + std::to_string(k + 1) +
               " (rule 2, algorithm-fundamental: ell=n/(k+1) must be a whole number -- got n=" + std::to_string(n) + ")";
    }
    unsigned ell = n / (k + 1);
    if (ell > 32) {
        return "n/(k+1) must be <= 32 (rule 3, original pow.h's own documented MAX_N=32-byte assumption -- got ell=" + std::to_string(ell) + ")";
    }
    return "";
}

static std::string exe_dir(const char* argv0) {
    std::string path(argv0);
    size_t pos = path.find_last_of('/');
    return pos == std::string::npos ? "." : path.substr(0, pos);
}

static std::string json_str(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += "\"";
    return out;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <n> <k> <seed> <nonce> [--reps N] [--no-save]\n", argv[0]);
        return 1;
    }
    unsigned n = (unsigned)atoi(argv[1]);
    unsigned k = (unsigned)atoi(argv[2]);
    std::string nk_err = check_nk(n, k);
    if (!nk_err.empty()) {
        fprintf(stderr, "%s: invalid (n=%u, k=%u): %s\n", argv[0], n, k, nk_err.c_str());
        fprintf(stderr, "%s: see Req/scripts/equihash_formulas.py --valid-n %u for the Req/Equihash-side valid-n list (a related but not identical rule set)\n", argv[0], k);
        return 1;
    }
    uint32_t seed = (uint32_t)strtoul(argv[3], nullptr, 10);
    uint32_t nonce = (uint32_t)strtoul(argv[4], nullptr, 10);

    int reps = 5; // Req/BENCH.md S2 floor
    bool no_save = false;
    for (int i = 5; i < argc; ++i) {
        if (std::string(argv[i]) == "--reps" && i + 1 < argc) reps = atoi(argv[++i]);
        else if (std::string(argv[i]) == "--no-save") no_save = true;
    }

    std::vector<double> wall_ms_samples;
    long peak_bytes = 0;
    for (int r = 0; r < reps; ++r) {
        Equihash eq(n, k, Seed(seed));
        eq.SetNonce(nonce);

        auto start = std::chrono::steady_clock::now();
        eq.InitializeMemory();
        eq.FillMemory(4UL << (n / (k + 1) - 1));
        for (unsigned i = 1; i <= k; ++i) {
            bool to_store = (i == k);
            eq.ResolveCollisions(to_store);
        }
        auto end = std::chrono::steady_clock::now();
        wall_ms_samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        peak_bytes = std::max(peak_bytes, peak_rss_bytes());
    }
    std::sort(wall_ms_samples.begin(), wall_ms_samples.end());
    double min_ms = wall_ms_samples.front();
    double median_ms = wall_ms_samples[wall_ms_samples.size() / 2];
    std::vector<double> devs;
    for (double v : wall_ms_samples) devs.push_back(std::abs(v - median_ms));
    std::sort(devs.begin(), devs.end());
    double mad_ms = devs[devs.size() / 2];

    std::string git_rev = run_git("rev-parse --short HEAD");
    if (git_rev.empty()) git_rev = "unknown";
    std::string status = run_git("status --porcelain");
    bool git_dirty = !status.empty();

    char nonce_or_seed[64];
    snprintf(nonce_or_seed, sizeof(nonce_or_seed), "seed=%u,nonce=%u", seed, nonce);

#if defined(__APPLE__)
    std::string machine = "arm64/x86_64 (macOS)";
#elif defined(__linux__)
    std::string machine = "unknown-arch (Linux)";
#else
    std::string machine = "unknown";
#endif

    // Exact field order/types must match RunRecord::to_json_line (Rust,
    // reqbench/src/run_record.rs) -- see that file's doc comment.
    char line[1024];
    snprintf(line, sizeof(line),
        "{\"impl\":\"rk-cpp\",\"lang\":\"cpp\",\"point\":{\"n\":%u,\"k_or_K\":%u,\"convention\":\"tree_depth_k\"},"
        "\"nonce_or_seed\":%s,\"reps\":%d,\"wall_min_ms\":%.4f,\"wall_median_ms\":%.4f,"
        "\"wall_mad_ms\":%.4f,\"peak_mem_bytes\":%ld,\"mem_instrument\":\"os_rss\","
        "\"solutions_found\":null,\"git_rev\":%s,\"git_dirty\":%s,\"machine\":%s}",
        n, k, json_str(nonce_or_seed).c_str(), reps, min_ms, median_ms, mad_ms, peak_bytes,
        json_str(git_rev).c_str(), git_dirty ? "true" : "false", json_str(machine).c_str());

    printf("%s\n", line);
    if (!no_save) {
        std::string runs_dir = exe_dir(argv[0]) + "/../runs";
        mkdir(runs_dir.c_str(), 0755); // fine if it already exists; errno ignored on EEXIST
        std::string path = runs_dir + "/" + run_filename(n, k);
        // O_EXCL: fail loudly on any name collision rather than silently
        // overwriting -- a collision needs two runs at the exact same
        // second, which this turns into an explicit error, not silent
        // data loss (matches the Rust sibling's create_new semantics).
        FILE* f = fopen(path.c_str(), "wx");
        if (!f) {
            fprintf(stderr, "warning: could not create %s (%s) -- run not saved\n",
                    path.c_str(), strerror(errno));
        } else {
            fprintf(f, "%s\n", line); // stdio is buffered by default; fclose flushes
            fclose(f);
            fprintf(stderr, "saved: %s\n", path.c_str());
        }
    }
    return 0;
}
