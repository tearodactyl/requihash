# Technical brief: proof of work for inference-centric hardware

## Start from the machine that actually exists

An “AI accelerator” is not merely a large matrix multiplier. Current systems join
low-precision tensor arrays to register files, several levels of SRAM/cache, HBM,
host memory, and scale-up interconnect. Their useful throughput depends on software
that batches requests, lays out tensors, overlaps communication, and selects
kernels. NVIDIA's [Blackwell architecture](https://www.nvidia.com/en-sg/data-center/technologies/blackwell-architecture/)
combines FP4/FP8 tensor support with HBM and NVLink-scale systems; AMD's
[MI300X](https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/data-sheets/amd-instinct-mi300x-platform-data-sheet.pdf)
emphasizes very large HBM capacity in a chiplet package. Emerging PIM work such as
[Pimba](https://arxiv.org/abs/2507.10178) moves memory-bound attention and
state-space updates closer to HBM banks.

A puzzle that performs only dense GEMM rewards tensor throughput and can be
implemented by a narrow systolic ASIC. A puzzle that performs only random DRAM
reads ignores expensive tensor hardware. To be broadly suitable for inference
machines, a workload must exercise the combination: low-bit matrix operations,
large resident state, irregular selection, reductions, local dependency, and
movement across tiers.

This objective is different from ASIC resistance. It intentionally targets a
class of specialized hardware. The business hypothesis is that inference
accelerators have a large, competitive supplier base and valuable alternative
uses, so mining capital remains liquid. That hypothesis must be tested against HBM
supply, cloud concentration, proprietary compilers, and the ease of renting a
short attack.

The closest precedents solve different pieces of the problem. [Equihash](https://eprint.iacr.org/2015/946.pdf)
creates a compact witness from a bandwidth-heavy collision search, while
[RandomX](https://github.com/tevador/RandomX) mixes generated code with a large
dataset to approximate general CPU machinery. The 2025 construction for
[useful matrix-multiplication work](https://eprint.iacr.org/2025/685.pdf) addresses
permissionless eligibility around a tensor operation. None by itself exercises
the full memory-and-tensor profile of an inference accelerator.

## Candidate 1: seeded sparse tensor graph

A block challenge expands into a directed graph of tensor blocks. Each node
specifies a small operation over challenge-derived data: ternary or INT4 matrix
multiplication, a permutation/gather from a large state array, a nonlinear integer
map, and a reduction. Edges choose prior node outputs. A nonce selects a subgraph
and initial state; the final digest is compared with difficulty.

```text
challenge + nonce
      |
  graph expander -----> block descriptors
      |                  [layout, scale, sparsity, parents]
      v
large state <--> gather -> tensor op -> reduce -> dependent write
      ^                                             |
      +---------------------------------------------+
```

Ternary weights make descriptors compact and map to emerging inference datapaths.
Sparse routing forces metadata and gather handling. The large state requires HBM
or host memory; dependent writes prevent the entire graph from becoming one
precompiled dense product.

Verification is the weakness. Re-executing the graph may be much cheaper than
mining only because the verifier checks one winning nonce rather than billions of
losers, which is acceptable if a single graph is modest. If the graph itself must
be very expensive, verification becomes unsuitable for full nodes. Merkle-opening
samples reduce verifier work but introduce probabilistic soundness and data
commitments. A SNARK moves cost to the miner and may dominate the intended tensor
work. This candidate should therefore begin as a moderate-cost hash function, not
as “verifiable inference.”

## Candidate 2: hierarchical state walk with tensor checkpoints

This design begins closer to RandomX. A challenge derives a multi-gigabyte dataset.
Each step reads addresses determined by previous values. After a group of dependent
reads, the gathered vectors form a quantized matrix operation whose result selects
the next region. Small state fits in local SRAM; the dataset fits in HBM or large
DRAM; periodic wide tensor operations use accelerator arrays.

The alternating phases frustrate a narrow matrix ASIC and a pure memory streamer:

\[
s_{t+1}=Q\left(W_t\,G(D, a(s_t))\right),\qquad
a_{t+1}=H(s_{t+1},a_t).
\]

Here (G) gathers vectors from dataset (D), (W_t) is challenge-derived and
low precision, (Q) is a fully specified quantizer, and (H) updates addresses.
The exact integer semantics must be portable. Floating-point non-associativity is
unacceptable in consensus.

Multi-nonce parallelism is the primary attack. A custom device can keep many walks
in flight, hide memory latency, and feed tensor arrays continuously. Dataset
bandwidth then becomes the bottleneck, potentially favoring HBM-equipped systems
exactly as intended—but also favoring the best custom memory controller. The
prototype must sweep thousands of interleaved nonces before making latency-hardness
claims.

## Candidate 3: useful-inference eligibility as a separate layer

A customer inference can create a commitment to inputs, model, numerical semantics,
and outputs. A challenge revealed after commitment selects intermediate tensors
for opening or recomputation. Successfully audited jobs earn an eligibility token
used in a conventional block lottery.

This separates customer privacy and variable job duration from block production,
but it does not fully solve useful work. Miners without jobs need a fallback;
customers may collude with miners; cached or replicated inference changes cost;
and audit probability must make fabrication irrational. The design is better
understood as a compute-market reward mechanism than as base consensus.

## Numerical semantics are part of consensus

Inference hardware gains speed from fused operations, approximate exponentials,
stochastic rounding, vendor-specific accumulation, and sparse skipping. Consensus
cannot accept “approximately the same tensor.” A PoW must define exact bit-level
results across vendors.

Integer dot products with explicit saturation or modular reduction are the safest
core. Per-block scales can be powers of two, turning rescaling into shifts. Ternary
weights permit add/subtract/skip. Nonlinear functions should be lookup tables or
integer polynomials with specified rounding. The resulting program may resemble
inference hardware without executing an ordinary neural network.

That distinction should be explicit. A workload shaped like AI arithmetic can
support accelerator liquidity and still produce no useful model output. Marketing
it as useful inference would confuse hardware compatibility with economic value.

## Business and security interpretation

Targeting inference hardware could broaden the mining asset base: the same machines
can serve models when mining revenue falls. It can also couple chain security to
the AI cycle. During inference demand spikes, honest hashpower becomes expensive;
during a demand collapse, idle accelerators flood mining. Large clouds can redirect
fleets rapidly, making attack capacity elastic. Consumer GPUs may participate but
lose badly to HBM systems if the working set and tensor mix are chosen around data
center accelerators.

The right market measure is a rental-adjusted security budget. Estimate the cost
to acquire enough compatible accelerator-hours for a reorganization, subtract
revenue from concurrent useful jobs, and include the premium for controlling the
required compiler/runtime. Residual hardware value helps miners but also lowers
the attacker's irrecoverable cost.

## Specific continuation methodology

Define a tiny, exact tensor virtual machine before designing a cryptocurrency. Its
instruction set should expose packed ternary/INT4 dot products, gather, permutation,
integer reduction, table nonlinearities, and challenge-dependent memory access.
Implement interpreters and optimized kernels in Rust SIMD, CUDA, HIP, and FPGA
RTL. Compare semantic conformance before speed.

Generate workloads across three working-set tiers: last-level cache, accelerator
HBM, and host/CXL memory. Measure arithmetic occupancy, bytes per operation, energy,
and performance under multi-nonce interleaving. Use the results to choose an
operation distribution; do not tune from vendor peak specifications.

Attempt three adversarial implementations: a pure tensor pipeline with synthesized
data, a memory streamer that avoids most tensor work, and a compressed/recomputed
dataset solver. The puzzle is interesting only if none obtains a structural
shortcut.

Open questions remain decisive. Can a puzzle use tensor hardware without reducing
to a cheap fixed-function systolic array? Can full nodes verify cheaply without a
proof system whose cost dominates mining? Is HBM availability sufficiently broad
for a permissionless market? Can exact integer semantics remain efficient on
inference accelerators designed around approximate low-precision formats? Until
those questions have measured answers, this is a hardware experiment rather than
a candidate consensus algorithm.
