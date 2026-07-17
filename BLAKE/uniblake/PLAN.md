# uniblake — PoC plan, selections, and reasoning

Companion to `../UniBlake.md` (the design). This file records the
**selections made to build the PoC**, the reasoning, and the stepwise
checkpoint plan. The design doc says what; this says what-we-chose and
in-what-order.

## PoC goal (per instruction)

Prove — by building and validating, not asserting — that the UniBlake
shape works: a persona-carrying reference (R2⊇R9), runtime CPU
**probing and dispatch on this ARM Mac M4** (R3⊇R7), variant
registration, `UB_FORCE_IMPL`, and the oracle self-test gate. Then
prepare and document build/validation paths for **x86 Ubuntu 24** and
**Windows 11** (native / WSL2 / cross-compile from Ubuntu). Stepwise,
with intermediate integration + validation checkpoints.

Out of PoC scope (later phases): SIMD kernels (U2), state snapshot
(U3), batch/"many" (U4), Rust wrapper (U5), BLAKE3 (U6). The PoC is
U0 + the demonstrable core of U1.

## Selections and reasoning

### S1 — Reference/oracle kernel: vendored `blake2b-ref.c`, dual-role

**Selection**: the streaming core is authored fresh (~§2's 100-line
core, so the compress boundary is a real dispatch seam), but the
**compress kernel `ref`** and the **pristine oracle backend** are both
the repo's already-vendored `../vendor/blake2/blake2b-ref.c`.

**Reasoning**: §1a tier-1 preference (reference implementation first).
Zero new provenance — it is the same bytes every C/C++ consumer already
builds and that PROVENANCE.md pins. Its `blake2b_compress(state,
block)` is exactly the pluggable unit §2 names, and it already exposes
`blake2b_init_param` (the R9 personalization path) with a modern
`blake2b()` argument order (avoids §3.3's trap by construction). To use
it *both* as a dispatched `ref` kernel *and* as an untouched
whole-implementation oracle, the PoC compiles it twice under two
symbol namespaces (see S4) — the oracle copy is byte-identical and
untouched, satisfying §1a invariant 1.

### S2 — Second kernel: an experimental unrolled scalar

**Selection**: a second registered compress kernel `ref_unrolled` —
same algorithm, the round loop hand-unrolled — purely to *exercise
variant selection and the self-test gate with something that is not the
oracle*.

**Reasoning**: a PoC with one kernel can't demonstrate R3⊇R7
(registration, selection, forced-impl) or R5-of-§5 (the gate catching a
wrong kernel). `ref_unrolled` gives a genuine second entry that (a) the
probe/`UB_FORCE_IMPL` can select between, and (b) is validated against
the oracle — and, as a deliberate test asset, a *broken* variant can be
compiled in under a build flag to prove the gate rejects it (§5
justification made observable). This is a **build/platform-class**
change to the algorithm's *expression*, not its *steps* (§1d) — the
unroll must stay byte-identical; the gate proves it.

### S3 — Location: `BLAKE/uniblake/`

**Selection**: the approval-target path, not a throwaway `-poc` dir.

**Reasoning**: the instruction is "start implementation, strive for
PoC" — this is the beginning of the real thing, built stepwise, not a
disposable spike. Anything that doesn't survive gets rewritten in
place under version control.

### S4 — Dispatch mechanism: real probe + table + `UB_FORCE_IMPL`, two kernels

**Selection**: a real runtime CPU probe (this pass: `sysctlbyname` on
arm64 macOS, reporting NEON always-present; x86 and Linux/Windows
probes stubbed-but-structured for the cross-platform step), a
function-pointer registration table, `UB_FORCE_IMPL` env override, and
the startup self-test gate over both kernels.

**Reasoning**: the instruction explicitly includes "probing and
dispatching on this ARM Mac M4 — show you can do this." A stubbed probe
would not meet that. The probe is written as a platform-dispatched
function so the macOS path is real now and the Linux/Windows paths are
filled at the cross-platform checkpoint (C3) with the same table.

**Deferred, not designed here** (per `../UniBlake.md` §2): whether the
dispatch table should be *exclusive*. The PoC uses the table, but the
`ref` build path is structured so a future compile-time-pinned inlined
kernel can coexist — the concern is flagged in the code, resolution is
a U1-proper task.

### S5 — Validation: all three §1d oracle types, wired from C0

**Selection**: (1) reference-agreement — every kernel's digest compared
byte-for-byte against the untouched oracle backend at startup and in
tests; (2) vectors — RFC 7693 `abc` vector + a committed
personalization vector (regenerated from the oracle); (3) operational
— a small driver hashing many messages through the public `ub_` API and
cross-checking the oracle, standing in for a consumer path.

**Reasoning**: §1d says all three are admissible and complementary; the
phase table's U0 gate requires "all three validation oracle types
green." Wiring them from the first checkpoint keeps every later step
honest.

## Stepwise checkpoint plan

Each checkpoint is independently buildable and validated before the
next. Integration/validation point stated per step.

| CP | Content | Integration + validation gate |
|---|---|---|
| **C0** | Repo skeleton: `uniblake/` layout, CMake, `ub_` public header (persona-carrying API, R2⊇R9), vendored-ref oracle backend linked under its own namespace, `abc` KAT | **builds on arm64 macOS; `abc` vector matches published bytes** |
| **C1** | Streaming core calling `compress` through a direct call to the single `ref` kernel (no table yet); wire reference-agreement + persona-vector + operational validation (§1d ×3) | **all three oracle types green on arm64** |
| **C2** | Dispatch: registration table, `ref` + `ref_unrolled` kernels, runtime `sysctlbyname` probe, `UB_FORCE_IMPL`, startup self-test gate; broken-kernel-under-flag proves the gate rejects | **probe selects on M4; forcing each kernel works + self-tests; broken variant hard-errors (§5 observable)** |
| **C3** | Cross-platform prep: fill Linux/Windows probe paths (structured, compile-guarded), CMake presets, and `BUILD.md` with exact x86-Ubuntu-24 and Windows-11 (native MSVC / WSL2 / Ubuntu cross-compile) test+validation directions | **arm64 still green; Linux/Windows paths compile-structured; directions written and self-consistent** |
| **C4** | Provenance manifest (§1a) for every input; PoC findings written back into `../UniBlake.md` (what the build confirmed/contradicted) | **manifest complete; design doc reconciled with reality** |

C0–C2 are the "prove it on M4" core. C3 is the "prepare x86/Windows"
instruction. C4 closes the loop back to the design.
