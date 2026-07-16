//! `blake2ref` — the repository's single BLAKE2b implementation, for Rust.
//!
//! Binds the vendored portable reference (`BLAKE/vendor/blake2`, see its
//! `PROVENANCE.md`) so Rust and C++ consumers share **one implementation
//! with one provenance** — byte-identity across languages holds by
//! construction, not by cross-validation. The state is an opaque byte
//! buffer sized by the C side at runtime (`blake2ref_state_size`), so this
//! crate never duplicates the C struct layout.
//!
//! API shape follows the requirements, not any historical packaging:
//! parameter-block init with `personal` (the flavor Equihash needs),
//! streaming `update`/`finalize`, and a cheaply clonable state for the
//! midstate pattern (clone = buffer copy).

use std::os::raw::{c_int, c_uchar, c_void};

extern "C" {
    fn blake2ref_state_size() -> usize;
    fn blake2ref_init_personal(state: *mut c_void, outlen: usize, personal: *const c_uchar)
        -> c_int;
    fn blake2ref_update(state: *mut c_void, input: *const c_uchar, inlen: usize) -> c_int;
    fn blake2ref_final(state: *mut c_void, out: *mut c_uchar, outlen: usize) -> c_int;
}

pub const PERSONALBYTES: usize = 16;
pub const OUTBYTES_MAX: usize = 64;

/// Inline state capacity. The vendored reference's `blake2b_state` is
/// ~240 bytes; the C-reported size is asserted against this bound once
/// per init, so a vendor update that grows the struct fails loudly
/// instead of corrupting. Inline (not boxed) so `Clone` is a plain
/// memcpy with no allocation — the midstate pattern clones per leaf.
const STATE_CAP: usize = 256;

/// A BLAKE2b hashing state. `Clone` copies the raw state buffer — the
/// midstate pattern (`state.clone()` per leaf, then a short `update` and
/// `finalize`) costs one small memcpy and no allocation, exactly like
/// the C side.
#[derive(Clone)]
pub struct Blake2b {
    state: [u8; STATE_CAP],
    outlen: usize,
}

impl Blake2b {
    /// Parameter-block init: digest length (1..=64) and 16-byte
    /// personalization (use `&[0u8; 16]` for none).
    pub fn with_personal(outlen: usize, personal: &[u8; PERSONALBYTES]) -> Self {
        assert!(outlen >= 1 && outlen <= OUTBYTES_MAX, "digest length 1..=64");
        assert!(
            unsafe { blake2ref_state_size() } <= STATE_CAP,
            "vendored blake2b_state grew past STATE_CAP; bump the constant"
        );
        let mut state = [0u8; STATE_CAP];
        let rc = unsafe {
            blake2ref_init_personal(state.as_mut_ptr() as *mut c_void, outlen, personal.as_ptr())
        };
        assert_eq!(rc, 0, "blake2b_init_param failed");
        Blake2b { state, outlen }
    }

    /// Plain (unpersonalized) init.
    pub fn new(outlen: usize) -> Self {
        Self::with_personal(outlen, &[0u8; PERSONALBYTES])
    }

    pub fn update(&mut self, input: &[u8]) -> &mut Self {
        let rc = unsafe {
            blake2ref_update(
                self.state.as_mut_ptr() as *mut c_void,
                input.as_ptr(),
                input.len(),
            )
        };
        assert_eq!(rc, 0, "blake2b_update failed");
        self
    }

    /// Consumes the state (the reference implementation's `final` is
    /// destructive) and returns the digest.
    pub fn finalize(mut self) -> Vec<u8> {
        let mut out = vec![0u8; self.outlen];
        let rc = unsafe {
            blake2ref_final(
                self.state.as_mut_ptr() as *mut c_void,
                out.as_mut_ptr(),
                self.outlen,
            )
        };
        assert_eq!(rc, 0, "blake2b_final failed");
        out
    }
}

/// One-shot convenience.
pub fn blake2b(outlen: usize, personal: &[u8; PERSONALBYTES], input: &[u8]) -> Vec<u8> {
    let mut h = Blake2b::with_personal(outlen, personal);
    h.update(input);
    h.finalize()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hex(b: &[u8]) -> String {
        b.iter().map(|x| format!("{x:02x}")).collect()
    }

    #[test]
    fn published_abc_vector() {
        // The canonical published BLAKE2b-512("abc") test vector.
        assert_eq!(
            hex(&blake2b(64, &[0u8; 16], b"abc")),
            "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1\
             7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923"
                .replace(' ', "")
        );
    }

    #[test]
    fn personalization_vectors_vs_python_hashlib() {
        // Independent oracle: CPython's own bundled BLAKE2
        // (hashlib.blake2b(digest_size=50, person=b"ZcashPoW"+le32(200)+le32(9))),
        // vectors generated 2026-07-16.
        let mut person = [0u8; 16];
        person[..8].copy_from_slice(b"ZcashPoW");
        person[8..12].copy_from_slice(&200u32.to_le_bytes());
        person[12..16].copy_from_slice(&9u32.to_le_bytes());

        assert_eq!(
            hex(&blake2b(50, &person, b"")),
            "42fadb7376483e2167dbb245215129da15280a65062e68cf07cc9bc3f71905b8\
             070472455b9fc809308919b7834c78b40726"
                .replace(' ', "")
        );

        let mut msg: Vec<u8> = (0u8..140).collect();
        msg.extend_from_slice(&7u32.to_le_bytes());
        assert_eq!(
            hex(&blake2b(50, &person, &msg)),
            "e79428f4372f46e5e955f15fb6398aad29cbcbc3e8fea5450c1abbcc5e458cab\
             32bbe848b5dc1a71b92b8c4fb78f47783fb1"
                .replace(' ', "")
        );
    }

    #[test]
    fn midstate_clone_equals_fresh_compute() {
        // The miner pattern: shared prefix hashed once, state cloned per
        // leaf. Must equal hashing each full message from scratch.
        let mut person = [0u8; 16];
        person[..8].copy_from_slice(b"Reqhash\x00"); // any 16B pattern
        person[15] = 0x57;
        let prefix: Vec<u8> = (0u8..140).collect();

        let mut mid = Blake2b::with_personal(50, &person);
        mid.update(&prefix);

        for leaf in [0u32, 1, 41, 0xFFFF_FFFF] {
            let mut cloned = mid.clone();
            cloned.update(&leaf.to_le_bytes());
            let via_midstate = cloned.finalize();

            let mut full = prefix.clone();
            full.extend_from_slice(&leaf.to_le_bytes());
            assert_eq!(via_midstate, blake2b(50, &person, &full), "leaf {leaf}");
        }
    }

    #[test]
    fn agrees_with_independent_rust_implementation() {
        // blake2b_simd (an unrelated pure-Rust implementation) as a second
        // independent oracle, across digest lengths and input sizes
        // spanning block boundaries.
        let mut person = [0u8; 16];
        person[..6].copy_from_slice(b"xcheck");
        for outlen in [1usize, 32, 50, 60, 64] {
            for msglen in [0usize, 1, 127, 128, 129, 255, 256, 1000] {
                let msg: Vec<u8> = (0..msglen).map(|i| (i * 7 % 251) as u8).collect();
                let theirs = blake2b_simd::Params::new()
                    .hash_length(outlen)
                    .personal(&person)
                    .hash(&msg);
                assert_eq!(
                    blake2b(outlen, &person, &msg),
                    theirs.as_bytes(),
                    "outlen={outlen} msglen={msglen}"
                );
            }
        }
    }
}
