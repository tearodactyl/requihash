// Minimal self-contained BLAKE2b (RFC 7693) with personalization, sufficient for
// Requihash's keyed-by-personalization initial state. Public-domain reference
// style; not constant-time-audited, for reference/testing use.
#ifndef REQ_BLAKE2B_H
#define REQ_BLAKE2B_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace reqb2 {

struct State {
    uint64_t h[8];
    uint64_t t[2];
    uint64_t f[2];
    uint8_t buf[128];
    size_t buflen;
    size_t outlen;
};

static const uint64_t IV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
    0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};

static const uint8_t SIGMA[12][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}};

inline uint64_t rotr64(uint64_t x, unsigned c) { return (x >> c) | (x << (64 - c)); }
inline uint64_t load64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}
inline void store64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}
inline void store32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

inline void compress(State& S, const uint8_t block[128], bool last) {
    uint64_t m[16], v[16];
    for (int i = 0; i < 16; i++) m[i] = load64(block + 8 * i);
    for (int i = 0; i < 8; i++) v[i] = S.h[i];
    for (int i = 0; i < 8; i++) v[8 + i] = IV[i];
    v[12] ^= S.t[0];
    v[13] ^= S.t[1];
    if (last) v[14] = ~v[14];
#define G(r, i, a, b, c, d)                          \
    a = a + b + m[SIGMA[r][2 * i + 0]];              \
    d = rotr64(d ^ a, 32);                           \
    c = c + d;                                        \
    b = rotr64(b ^ c, 24);                           \
    a = a + b + m[SIGMA[r][2 * i + 1]];              \
    d = rotr64(d ^ a, 16);                           \
    c = c + d;                                        \
    b = rotr64(b ^ c, 63);
    for (int r = 0; r < 12; r++) {
        G(r, 0, v[0], v[4], v[8], v[12]);
        G(r, 1, v[1], v[5], v[9], v[13]);
        G(r, 2, v[2], v[6], v[10], v[14]);
        G(r, 3, v[3], v[7], v[11], v[15]);
        G(r, 4, v[0], v[5], v[10], v[15]);
        G(r, 5, v[1], v[6], v[11], v[12]);
        G(r, 6, v[2], v[7], v[8], v[13]);
        G(r, 7, v[3], v[4], v[9], v[14]);
    }
#undef G
    for (int i = 0; i < 8; i++) S.h[i] ^= v[i] ^ v[8 + i];
}

// Initialise BLAKE2b with an output length and a 16-byte personalization,
// matching the zcash Equihash convention (person = "ZcashPoW" || le32(n) || le32(k)).
inline State init(size_t outlen, const uint8_t person[16]) {
    State S;
    memset(&S, 0, sizeof(S));
    S.outlen = outlen;
    for (int i = 0; i < 8; i++) S.h[i] = IV[i];
    // Parameter block: digest_length=outlen, key=0, fanout=1, depth=1,
    // personalization in bytes 48..63.
    uint8_t p[64];
    memset(p, 0, 64);
    p[0] = (uint8_t)outlen;
    p[2] = 1;  // fanout
    p[3] = 1;  // depth
    memcpy(p + 48, person, 16);
    for (int i = 0; i < 8; i++) S.h[i] ^= load64(p + 8 * i);
    return S;
}

inline void update(State& S, const uint8_t* in, size_t inlen) {
    while (inlen > 0) {
        if (S.buflen == 128) {
            S.t[0] += 128;
            if (S.t[0] < 128) S.t[1]++;
            compress(S, S.buf, false);
            S.buflen = 0;
        }
        size_t take = 128 - S.buflen;
        if (take > inlen) take = inlen;
        memcpy(S.buf + S.buflen, in, take);
        S.buflen += take;
        in += take;
        inlen -= take;
    }
}

inline void final(State& S, uint8_t* out) {
    S.t[0] += S.buflen;
    if (S.t[0] < S.buflen) S.t[1]++;
    memset(S.buf + S.buflen, 0, 128 - S.buflen);
    compress(S, S.buf, true);
    uint8_t full[64];
    for (int i = 0; i < 8; i++) store64(full + 8 * i, S.h[i]);
    memcpy(out, full, S.outlen);
}

}  // namespace reqb2

#endif
