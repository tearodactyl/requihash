// Benchmark driver: cs_v4_bench <n> <K> <nonce_hex> [--reps N] [--no-save]
// runs cs_v4::KListWagnerAlgorithmV4::solve() repeatedly (default 5 reps, Req/BENCH.md S2's
// floor) and reports min/median/MAD wall time + a process-level peak-RSS
// reading, as one JSON line matching reqbench::run_record::RunRecord's
// exact field set/order/types (Rust crate,
// Req/SOLVER_CORPUS/reqbench/src/run_record.rs -- see that file's own doc
// comment: this is the contract a C++ driver's printf must match since
// there is no shared C++ library across SOLVER_CORPUS ports).
//
// Every invocation writes a NEW file to
// Req/SOLVER_CORPUS/cs/variants/v4-static-inplace/runs/<n><K>_<timestamp>.jsonl -- never
// appends, never touches another implementation's directory. Pass
// --no-save to print only.
//
// NOTE the point convention: n/K here are CS/Sequihash's own list-count
// convention (K a power of 2, solution size == K), NOT Requihash's
// tree-depth k (solution size == 2^k) -- point.convention is recorded as
// "list_count_K" in the JSON so a reader never has to guess which this
// is. See klist_v4.hpp's own two-conventions note.
#include "klist_v4.hpp"
#include "nk_check.hpp"

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

static std::string json_str(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += "\"";
    return out;
}

// Matches reqbench::run_record::run_filename: "<n><K>_YYYYMMDDTHHMMSSZ.jsonl"
static std::string run_filename(unsigned n, unsigned K) {
    std::time_t t = std::time(nullptr);
    std::tm utc;
    gmtime_r(&t, &utc);
    char buf[48];
    snprintf(buf, sizeof(buf), "%u%u_%04d%02d%02dT%02d%02d%02dZ.jsonl",
        n, K,
        utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
        utc.tm_hour, utc.tm_min, utc.tm_sec);
    return buf;
}

static std::string exe_dir(const char* argv0) {
    std::string path(argv0);
    size_t pos = path.find_last_of('/');
    return pos == std::string::npos ? "." : path.substr(0, pos);
}

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> out(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = (uint8_t)strtoul(hex.substr(i * 2, 2).c_str(), nullptr, 16);
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <n> <K> <nonce_hex> [--reps N] [--no-save]\n", argv[0]);
        return 1;
    }
    unsigned n = (unsigned)atoi(argv[1]);
    unsigned K = (unsigned)atoi(argv[2]);
    cs_common::require_valid_nk(n, K, argv[0]);
    std::string nonce_hex = argv[3];
    std::vector<uint8_t> nonce = hex_to_bytes(nonce_hex);

    int reps = 5;
    bool no_save = false;
    for (int i = 4; i < argc; ++i) {
        if (std::string(argv[i]) == "--reps" && i + 1 < argc) reps = atoi(argv[++i]);
        else if (std::string(argv[i]) == "--no-save") no_save = true;
    }

    std::vector<double> wall_ms_samples;
    long peak_bytes = 0;
    unsigned long long solutions_found = 0;
    for (int r = 0; r < reps; ++r) {
        auto start = std::chrono::steady_clock::now();
        cs_v4::KListWagnerAlgorithmV4 solver(n, K, nonce);
        auto sols = solver.solve();
        auto end = std::chrono::steady_clock::now();
        wall_ms_samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        peak_bytes = std::max(peak_bytes, peak_rss_bytes());
        solutions_found = sols.size();
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

#if defined(__APPLE__)
    std::string machine = "arm64/x86_64 (macOS)";
#elif defined(__linux__)
    std::string machine = "unknown-arch (Linux)";
#else
    std::string machine = "unknown";
#endif

    char line[1024];
    snprintf(line, sizeof(line),
        "{\"impl\":\"cs-v4\",\"lang\":\"cpp\",\"point\":{\"n\":%u,\"k_or_K\":%u,\"convention\":\"list_count_K\"},"
        "\"nonce_or_seed\":%s,\"reps\":%d,\"wall_min_ms\":%.4f,\"wall_median_ms\":%.4f,"
        "\"wall_mad_ms\":%.4f,\"peak_mem_bytes\":%ld,\"mem_instrument\":\"os_rss\","
        "\"solutions_found\":%llu,\"git_rev\":%s,\"git_dirty\":%s,\"machine\":%s}",
        n, K, json_str(nonce_hex).c_str(), reps, min_ms, median_ms, mad_ms, peak_bytes,
        solutions_found, json_str(git_rev).c_str(), git_dirty ? "true" : "false", json_str(machine).c_str());

    printf("%s\n", line);
    if (!no_save) {
        std::string runs_dir = exe_dir(argv[0]) + "/../runs";
        mkdir(runs_dir.c_str(), 0755);
        std::string path = runs_dir + "/" + run_filename(n, K);
        FILE* f = fopen(path.c_str(), "wx");
        if (!f) {
            fprintf(stderr, "warning: could not create %s (%s) -- run not saved\n",
                    path.c_str(), strerror(errno));
        } else {
            fprintf(f, "%s\n", line);
            fclose(f);
            fprintf(stderr, "saved: %s\n", path.c_str());
        }
    }
    return 0;
}
