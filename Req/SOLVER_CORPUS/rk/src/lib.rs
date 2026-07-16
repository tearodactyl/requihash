//! Native Rust port of Khovratovich's original CC0 C++11 Equihash
//! reference solver: `equihash-khovratovich/Source/C++11/pow.{h,cc}`
//! (117 + 218 lines). A faithful translation, not a redesign: method
//! names (`initialize_memory`, `fill_memory`, `resolve_collisions`,
//! `find_proof`, `resolve_tree`) and control flow mirror the original's
//! `Equihash` class one-for-one. `std::vector<std::vector<Tuple>>`
//! becomes `Vec<Vec<Tuple>>`; the recursive `ResolveTreeByLevel` becomes
//! a recursive `resolve_tree_by_level`.
//!
//! Parameter range: matches the original exactly. `n` up to 32 bytes
//! (256 bits) is nominally representable by the original's `MAX_N`
//! constant, but the *practical* range is governed by memory/time, not
//! any hardcoded `(n,k)` restriction — the algorithm is a generic
//! recursive tree-fold over dynamically-sized tuple lists, not
//! bucket-specialized C. This is **broader than the RT port's range**:
//! RT (tromp's solver) is compile-time-restricted to specific `(WN,
//! RESTBITS)` pairs via `#if`/`#elif` branches; RK has no such
//! restriction to work around. Reaching Zero/Zcash's `(192,7)`/`(200,9)`
//! needs no port change — only parameters large enough that a caller is
//! willing to pay this generic solver's cost (see `README.md` for
//! measured scaling; this solver is not memory- or time-optimized, and
//! `(192,7)`/`(200,9)` are impractical with it on ordinary hardware).
//!
//! No I/O, no CLI — this module is a library only.

use blake2b_simd::Params as Blake2bParams;

pub const SEED_LENGTH: usize = 4; // dwords
pub const MAX_NONCE: u32 = 0xFFFFF;
pub const LIST_LENGTH: usize = 5;
pub const FORK_MULTIPLIER: usize = 3;

/// Four-word seed, matching the original's `Seed` (a `std::vector<uint32_t>`
/// of length `SEED_LENGTH`, all elements equal when constructed from a
/// single `uint32_t`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Seed(pub [u32; SEED_LENGTH]);

impl Seed {
    pub fn from_u32(x: u32) -> Self {
        Seed([x; SEED_LENGTH])
    }
}

impl Default for Seed {
    fn default() -> Self {
        Seed([0; SEED_LENGTH])
    }
}

pub type Nonce = u32;
pub type Input = u32;

/// A completed proof: `n`, `k`, the seed/nonce that produced it, and the
/// `2^k` leaf indices forming the solution. Mirrors the original's
/// `Proof` struct.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Proof {
    pub n: u32,
    pub k: u32,
    pub seed: Seed,
    pub nonce: Nonce,
    pub inputs: Vec<Input>,
}

impl Proof {
    /// Recomputes each leaf's `(k+1)` XOR-accumulator blocks from
    /// scratch and checks they all cancel to zero — mirrors the
    /// original's `Proof::Test`.
    pub fn test(&self) -> bool {
        let mut blocks = vec![0u32; (self.k + 1) as usize];
        let bits = self.n / (self.k + 1);
        for &leaf in &self.inputs {
            let buf = hash_leaf(&self.seed, self.nonce, leaf);
            for (j, block) in blocks.iter_mut().enumerate() {
                *block ^= buf[j] >> (32 - bits);
            }
        }
        let all_zero = blocks.iter().all(|&b| b == 0);
        all_zero && !self.inputs.is_empty()
    }
}

/// A stored row: `k` remaining XOR-accumulator blocks (one fewer per
/// round) plus a back-reference into the prior round's fork list (or,
/// at round 0, the raw leaf index). Mirrors the original's `Tuple`.
#[derive(Clone, Debug, Default)]
struct Tuple {
    blocks: Vec<u32>,
    reference: Input,
}

impl Tuple {
    fn new(len: usize) -> Self {
        Tuple {
            blocks: vec![0u32; len],
            reference: 0,
        }
    }
}

/// A binary collision fork: the two prior-round references that
/// collided. Mirrors the original's `Fork`.
#[derive(Clone, Copy, Debug, Default)]
struct Fork {
    ref1: Input,
    ref2: Input,
}

/// BLAKE2b-512 over `[seed[0..4], nonce, leaf_index]` (6 little-endian
/// u32 words = 24 bytes), unkeyed, no personalization, 32-byte digest —
/// matching `pow.cc`'s `blake2b((uint8_t*)buf, &input, NULL,
/// sizeof(buf), sizeof(input), 0)` where `input` is a
/// `uint32_t[SEED_LENGTH+2]` and `buf` is `uint32_t[MAX_N/4]` (8 words,
/// but only the digest's leading bytes matter — `sizeof(buf)==32` here
/// since `MAX_N=32` bytes and the call passes `sizeof(buf)` as the
/// C `blake2b`'s `outlen`, which is `MAX_N/4 * sizeof(uint32_t) = 32`).
/// Returned as 8 native-endian u32 words, matching the original's
/// `uint32_t buf[MAX_N/4]` reinterpretation of the output bytes on a
/// little-endian machine.
fn hash_leaf(seed: &Seed, nonce: Nonce, leaf: Input) -> [u32; 8] {
    let mut input = [0u8; (SEED_LENGTH + 2) * 4];
    for i in 0..SEED_LENGTH {
        input[i * 4..i * 4 + 4].copy_from_slice(&seed.0[i].to_le_bytes());
    }
    input[SEED_LENGTH * 4..SEED_LENGTH * 4 + 4].copy_from_slice(&nonce.to_le_bytes());
    input[(SEED_LENGTH + 1) * 4..(SEED_LENGTH + 1) * 4 + 4].copy_from_slice(&leaf.to_le_bytes());

    let digest = Blake2bParams::new()
        .hash_length(32)
        .to_state()
        .update(&input)
        .finalize();
    let bytes = digest.as_bytes();
    let mut buf = [0u32; 8];
    for i in 0..8 {
        buf[i] = u32::from_le_bytes([
            bytes[i * 4],
            bytes[i * 4 + 1],
            bytes[i * 4 + 2],
            bytes[i * 4 + 3],
        ]);
    }
    buf
}

/// Faithful port of the original `Equihash` class. Preserves method
/// names and structure per the port task's requirement.
pub struct Equihash {
    tuple_list: Vec<Vec<Tuple>>,
    filled_list: Vec<usize>,
    solutions: Vec<Proof>,
    forks: Vec<Vec<Fork>>,
    n: u32,
    k: u32,
    seed: Seed,
    nonce: Nonce,
}

impl Equihash {
    pub fn new(n: u32, k: u32, seed: Seed) -> Self {
        Equihash {
            tuple_list: Vec::new(),
            filled_list: Vec::new(),
            solutions: Vec::new(),
            forks: Vec::new(),
            n,
            k,
            seed,
            nonce: 0,
        }
    }

    /// Allocates working memory sized from `(n, k)`. Mirrors
    /// `InitializeMemory`.
    pub fn initialize_memory(&mut self) {
        let bits = self.n / (self.k + 1);
        let tuple_n = 1usize << bits;
        let default_tuple = Tuple::new(self.k as usize); // k blocks (one left for index)
        self.tuple_list = vec![vec![default_tuple; LIST_LENGTH]; tuple_n];
        self.filled_list = vec![0usize; tuple_n];
        self.solutions.clear();
        self.forks.clear();
    }

    /// Generates `length` leaf hashes and buckets each into
    /// `tuple_list` by its top `n/(k+1)` bits, keeping up to
    /// `LIST_LENGTH` per bucket. Mirrors `FillMemory` ("works for
    /// k<=7" per the original's own comment — a property of the
    /// generic algorithm, not narrowed further by this port).
    pub fn fill_memory(&mut self, length: u32) {
        let bits = self.n / (self.k + 1);
        for i in 0..length {
            let buf = hash_leaf(&self.seed, self.nonce, i);
            let index = (buf[0] >> (32 - bits)) as usize;
            let count = self.filled_list[index];
            if count < LIST_LENGTH {
                for j in 1..(self.k as usize + 1) {
                    self.tuple_list[index][count].blocks[j - 1] = buf[j] >> (32 - bits);
                }
                self.tuple_list[index][count].reference = i;
                self.filled_list[index] += 1;
            }
        }
    }

    /// Recursively expands a fork chain back to raw leaf indices.
    /// Mirrors `ResolveTreeByLevel`.
    fn resolve_tree_by_level(&self, fork: Fork, level: usize) -> Vec<Input> {
        if level == 0 {
            return vec![fork.ref1, fork.ref2];
        }
        let mut v1 = self.resolve_tree_by_level(self.forks[level - 1][fork.ref1 as usize], level - 1);
        let v2 = self.resolve_tree_by_level(self.forks[level - 1][fork.ref2 as usize], level - 1);
        v1.extend(v2);
        v1
    }

    /// Mirrors `ResolveTree`.
    fn resolve_tree(&self, fork: Fork) -> Vec<Input> {
        self.resolve_tree_by_level(fork, self.forks.len())
    }

    /// One round of pairwise collision resolution within each bucket:
    /// XORs the leading remaining block to find the next bucket index,
    /// and either stores the merged row (if not the last round) or
    /// checks for a full solution (all blocks zero, `store == true`).
    /// Mirrors `ResolveCollisions`.
    pub fn resolve_collisions(&mut self, store: bool) {
        let table_length = self.tuple_list.len();
        let max_new_collisions = table_length * FORK_MULTIPLIER;
        let new_blocks = self.tuple_list[0][0].blocks.len() - 1;
        let mut new_forks = vec![Fork::default(); max_new_collisions];
        let mut collision_list =
            vec![vec![Tuple::new(new_blocks); LIST_LENGTH]; table_length];
        let mut new_filled_list = vec![0usize; table_length];
        let mut new_colls: usize = 0;

        for i in 0..table_length {
            let filled = self.filled_list[i];
            for j in 0..filled {
                for m in (j + 1)..filled {
                    let new_index =
                        (self.tuple_list[i][j].blocks[0] ^ self.tuple_list[i][m].blocks[0]) as usize;
                    let new_fork = Fork {
                        ref1: self.tuple_list[i][j].reference,
                        ref2: self.tuple_list[i][m].reference,
                    };
                    if store {
                        // last step
                        if new_index == 0 {
                            let solution_inputs = self.resolve_tree(new_fork);
                            self.solutions.push(Proof {
                                n: self.n,
                                k: self.k,
                                seed: self.seed,
                                nonce: self.nonce,
                                inputs: solution_inputs,
                            });
                        }
                    } else if new_filled_list[new_index] < LIST_LENGTH && new_colls < max_new_collisions
                    {
                        for l in 0..new_blocks {
                            collision_list[new_index][new_filled_list[new_index]].blocks[l] =
                                self.tuple_list[i][j].blocks[l + 1] ^ self.tuple_list[i][m].blocks[l + 1];
                        }
                        new_forks[new_colls] = new_fork;
                        collision_list[new_index][new_filled_list[new_index]].reference = new_colls as u32;
                        new_filled_list[new_index] += 1;
                        new_colls += 1;
                    }
                }
            }
        }

        self.forks.push(new_forks);
        std::mem::swap(&mut self.tuple_list, &mut collision_list);
        std::mem::swap(&mut self.filled_list, &mut new_filled_list);
    }

    /// Searches nonces from 1 upward until a duplicate-free solution is
    /// found (or `MAX_NONCE` is exhausted, returning an empty proof).
    /// Mirrors `FindProof`; drops the original's stdout/`proof.log`
    /// profiling output (harness concern, not algorithm) but preserves
    /// its control flow exactly, including the duplicate-index check.
    pub fn find_proof(&mut self) -> Proof {
        self.nonce = 1;
        while self.nonce < MAX_NONCE {
            self.nonce += 1;
            self.initialize_memory();
            let bits = self.n / (self.k + 1);
            self.fill_memory(4u32 << (bits - 1));
            for i in 1..=self.k {
                let to_store = i == self.k;
                self.resolve_collisions(to_store);
            }

            for sol in &self.solutions {
                let mut vec = sol.inputs.clone();
                vec.sort_unstable();
                let dup = vec.windows(2).any(|w| w[0] == w[1]);
                if !dup {
                    return sol.clone();
                }
            }
        }
        Proof {
            n: self.n,
            k: self.k,
            seed: self.seed,
            nonce: self.nonce,
            inputs: Vec::new(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn finds_and_verifies_a_solution_at_small_params() {
        let mut eq = Equihash::new(60, 4, Seed::from_u32(3));
        let proof = eq.find_proof();
        assert!(!proof.inputs.is_empty(), "expected a solution to be found");
        assert!(proof.test(), "solution failed self-verification");
    }

    #[test]
    fn solution_size_is_2_pow_k() {
        let mut eq = Equihash::new(90, 5, Seed::from_u32(7));
        let proof = eq.find_proof();
        assert_eq!(proof.inputs.len(), 1usize << 5);
    }
}
