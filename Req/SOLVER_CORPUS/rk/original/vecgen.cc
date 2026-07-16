// Vector generator: calls the unmodified Equihash::FindProof() and prints
// one {"n","k","seed","nonce","indices","verified"} JSON line -- the
// generator for ../vectors/*.json. Harness code, not algorithm; pow.h /
// the Equihash class are untouched by this file.
#include "pow.h"
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define DUP _dup
#define DUP2 _dup2
#define OPEN_NULL() _open("NUL", _O_WRONLY)
#define CLOSE _close
#else
#include <unistd.h>
#include <fcntl.h>
#define DUP dup
#define DUP2 dup2
#define OPEN_NULL() open("/dev/null", O_WRONLY)
#define CLOSE close
#endif

using namespace _POW;

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <n> <k> <seed>\n", argv[0]);
        return 1;
    }
    unsigned n = (unsigned)atoi(argv[1]);
    unsigned k = (unsigned)atoi(argv[2]);
    uint32_t s = (uint32_t)atol(argv[3]);
    Equihash eq(n, k, Seed(s));

    // FindProof()/Test() print unstructured progress output via their own
    // printfs (upstream code, unmodified) -- silence fd 1 while they run,
    // restore before emitting our one JSON line.
    fflush(stdout);
    int saved_fd = DUP(1);
    int devnull_fd = OPEN_NULL();
    DUP2(devnull_fd, 1);
    CLOSE(devnull_fd);

    Proof p = eq.FindProof();
    bool ok = p.Test();

    fflush(stdout);
    DUP2(saved_fd, 1);
    CLOSE(saved_fd);

    printf("{\"n\":%u,\"k\":%u,\"seed\":%u,\"nonce\":%u,\"indices\":[", p.n, p.k, s, p.nonce);
    for (size_t i = 0; i < p.inputs.size(); ++i) {
        if (i) printf(",");
        printf("%u", p.inputs[i]);
    }
    printf("],\"verified\":%s}\n", ok ? "true" : "false");
    return 0;
}
