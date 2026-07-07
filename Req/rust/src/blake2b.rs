//! Minimal BLAKE2b (RFC 7693) with personalization, byte-for-byte matching the
//! C++ `cpp/blake2b.h`. Sufficient for Requihash's keyed initial state. Not
//! constant-time-audited; reference/testing use.

const IV: [u64; 8] = [
    0x6a09e667f3bcc908,
    0xbb67ae8584caa73b,
    0x3c6ef372fe94f82b,
    0xa54ff53a5f1d36f1,
    0x510e527fade682d1,
    0x9b05688c2b3e6c1f,
    0x1f83d9abfb41bd6b,
    0x5be0cd19137e2179,
];

const SIGMA: [[usize; 16]; 12] = [
    [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
    [14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3],
    [11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4],
    [7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8],
    [9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13],
    [2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9],
    [12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11],
    [13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10],
    [6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5],
    [10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0],
    [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
    [14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3],
];

pub struct State {
    h: [u64; 8],
    t: [u64; 2],
    buf: [u8; 128],
    buflen: usize,
    outlen: usize,
}

fn load64(p: &[u8]) -> u64 {
    let mut v = 0u64;
    for i in 0..8 {
        v |= (p[i] as u64) << (8 * i);
    }
    v
}

fn compress(s: &mut State, block: &[u8], last: bool) {
    let mut m = [0u64; 16];
    for i in 0..16 {
        m[i] = load64(&block[8 * i..8 * i + 8]);
    }
    let mut v = [0u64; 16];
    v[..8].copy_from_slice(&s.h);
    v[8..].copy_from_slice(&IV);
    v[12] ^= s.t[0];
    v[13] ^= s.t[1];
    if last {
        v[14] = !v[14];
    }

    macro_rules! g {
        ($r:expr, $i:expr, $a:expr, $b:expr, $c:expr, $d:expr) => {
            v[$a] = v[$a].wrapping_add(v[$b]).wrapping_add(m[SIGMA[$r][2 * $i]]);
            v[$d] = (v[$d] ^ v[$a]).rotate_right(32);
            v[$c] = v[$c].wrapping_add(v[$d]);
            v[$b] = (v[$b] ^ v[$c]).rotate_right(24);
            v[$a] = v[$a].wrapping_add(v[$b]).wrapping_add(m[SIGMA[$r][2 * $i + 1]]);
            v[$d] = (v[$d] ^ v[$a]).rotate_right(16);
            v[$c] = v[$c].wrapping_add(v[$d]);
            v[$b] = (v[$b] ^ v[$c]).rotate_right(63);
        };
    }
    for r in 0..12 {
        g!(r, 0, 0, 4, 8, 12);
        g!(r, 1, 1, 5, 9, 13);
        g!(r, 2, 2, 6, 10, 14);
        g!(r, 3, 3, 7, 11, 15);
        g!(r, 4, 0, 5, 10, 15);
        g!(r, 5, 1, 6, 11, 12);
        g!(r, 6, 2, 7, 8, 13);
        g!(r, 7, 3, 4, 9, 14);
    }
    for i in 0..8 {
        s.h[i] ^= v[i] ^ v[8 + i];
    }
}

/// Initialise with output length and 16-byte personalization.
pub fn init(outlen: usize, person: &[u8; 16]) -> State {
    let mut s = State {
        h: IV,
        t: [0, 0],
        buf: [0; 128],
        buflen: 0,
        outlen,
    };
    let mut p = [0u8; 64];
    p[0] = outlen as u8;
    p[2] = 1; // fanout
    p[3] = 1; // depth
    p[48..64].copy_from_slice(person);
    for i in 0..8 {
        s.h[i] ^= load64(&p[8 * i..8 * i + 8]);
    }
    s
}

pub fn update(s: &mut State, mut input: &[u8]) {
    while !input.is_empty() {
        if s.buflen == 128 {
            s.t[0] = s.t[0].wrapping_add(128);
            if s.t[0] < 128 {
                s.t[1] += 1;
            }
            let block = s.buf;
            compress(s, &block, false);
            s.buflen = 0;
        }
        let take = std::cmp::min(128 - s.buflen, input.len());
        s.buf[s.buflen..s.buflen + take].copy_from_slice(&input[..take]);
        s.buflen += take;
        input = &input[take..];
    }
}

pub fn finalize(mut s: State, out: &mut [u8]) {
    s.t[0] = s.t[0].wrapping_add(s.buflen as u64);
    if s.t[0] < s.buflen as u64 {
        s.t[1] += 1;
    }
    for i in s.buflen..128 {
        s.buf[i] = 0;
    }
    let block = s.buf;
    compress(&mut s, &block, true);
    let mut full = [0u8; 64];
    for i in 0..8 {
        full[8 * i..8 * i + 8].copy_from_slice(&s.h[i].to_le_bytes());
    }
    out[..s.outlen].copy_from_slice(&full[..s.outlen]);
}

impl Clone for State {
    fn clone(&self) -> Self {
        State {
            h: self.h,
            t: self.t,
            buf: self.buf,
            buflen: self.buflen,
            outlen: self.outlen,
        }
    }
}
