# Compute, Cryptography, and Durable Machine Intelligence

This dossier examines a connected set of technology and business questions:
memory-hard proof of work, post-quantum cryptocurrency, useful computation as
consensus work, algorithms that fit inference accelerators, alternatives to the
present Transformer-serving stack, and private personalization that can survive
many generations of base models.

The documents are written for a technically experienced reader who also cares
about capital cost, deployment paths, competitive advantage, and failure modes.
They are current through July 2, 2026. Claims about recent systems are linked to
primary papers, specifications, project repositories, or vendor documentation
where available. Proposals in these documents are research hypotheses, not
production cryptographic designs.

## Documents

- [Orientation essay](./essay.md) — the shared technical and commercial terrain,
  without forcing the original train of thought into a report taxonomy.
- [Memory-hard proof of work](./brief-memory-hard-pow.md) — Equihash, later
  solvers, competing designs, current optimization opportunities, and prototype
  methodology.
- [Post-quantum cryptocurrency](./brief-post-quantum-cryptocurrency.md) — quantum
  capability, signatures, consensus, commitments, zero knowledge, migration,
  and chain-specific business exposure.
- [Proof of useful work](./brief-useful-work.md) — when useful output can and
  cannot provide Nakamoto-style security, with storage, optimization, learning,
  and matrix multiplication as concrete cases.
- [PoW for inference hardware](./brief-inference-hardware-pow.md) — candidate
  puzzles for GPUs and emerging inference accelerators, and why “AI-shaped” work
  is not automatically useful or secure.
- [Hardware-native model architectures](./brief-hardware-native-models.md) —
  memory hierarchy, recurrent and sparse alternatives, low-bit representations,
  and a proposed compiled block architecture.
- [Private lifelong personalization](./brief-lifelong-personalization.md) — a
  durable personal learning substrate that is more structured than accumulated
  text and more portable than private model weights.

The briefs deliberately repeat only the small amount of context required to read
them independently. Open questions and specific continuation methods appear at
the end of each brief.
