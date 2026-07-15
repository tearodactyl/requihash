# `_template/` — starting shape for a new `SOLVER_CORPUS` port

Not a buildable crate itself (no `Cargo.toml` — deliberately, so it's never
accidentally picked up by a workspace glob or `cargo build --workspace`).
Copy the pieces you need into a new port directory (`rk/`, `rt/`, `cs/`,
...) and fill in the `TODO` markers. This exists because RZ's own bench/
env-check harness (`../rz/`) was built ad hoc, one piece at a time, in an
earlier session — the second port through this process shouldn't have to
rediscover the same shape RZ ended up at. See `Req/BENCH.md` for why each
piece looks the way it does.

## What to copy, and what stays port-specific

| File | Copy and adapt | Why |
|---|---|---|
| `check_env.sh` | Yes — replace the `TODO` prerequisite checks with this port's actual dependencies | Every port has its own external dependencies (a vendored source, a reference build, a specific tool); the *shape* (list checks, report OK/MISSING, exit nonzero on any miss) is shared, the checks themselves are not |
| `src/bin/bench_template.rs` | Yes — rename to `<port>_bench.rs`, replace the `TODO` call to the port's own solve/verify function | Depends on the shared `reqbench` crate (`../reqbench/`) — do not reimplement timing statistics, provenance stamping, or the memory cross-check per-port; that duplication is exactly what `Req/BENCH.md` exists to prevent |
| `Cargo.toml.snippet` | Yes — merge the `reqbench` dependency line and `[[bin]]` entry into the port's own `Cargo.toml` | Not a full `Cargo.toml`, since every port's other dependencies (the vendored source's own needs) differ |
| `ENVIRONMENT.md` | **No separate template — write directly from `../rz/ENVIRONMENT.md`** | RZ's own `ENVIRONMENT.md` is the concrete worked example; write this port's own by following its exact section structure (what was tested; dependencies and where each comes from, flagging any hardcoded local path as fragile; what the build does in order; how `check_env.sh` mirrors it) rather than filling in a separate blanks-only skeleton — that skeleton added a redundant intermediate copy without adding structure `../rz/ENVIRONMENT.md` doesn't already show directly |

## The five things every port's bench harness should do

Per `Req/BENCH.md`, in order of how much it costs to skip:

1. **Repeated trials, not one sample** — at least `reqbench::DEFAULT_REPS`
   (7) reps per case, via `reqbench::sample()`. A single-run number is an
   anecdote, not a baseline.
2. **Provenance on every record** — `reqbench::provenance::Provenance::capture()`
   stamps git commit/dirty-tree/build-profile automatically; call it once
   per run and pass it into every `to_json_line()` call.
3. **Memory cross-checked two ways** — if the port measures peak memory at
   all, install `reqbench::mem::CountingAlloc` as the `#[global_allocator]`
   *and* call `reqbench::mem::cross_check()` against OS RSS. Report
   disagreement as a finding, not something to silently average away.
4. **Parameter/window identity in every key** — the JSON `key` field (and
   any printed label) must state the exact parameter set and input/nonce
   identity, not just "the usual case." If this port's naming convention
   has the same RESTBITS-vs-k ambiguity RZ's `solve_144_4` does, say which
   convention is in play rather than leaving a bare number to be misread.
5. **A `baselines/` directory, committed**, mirroring `../rz/baselines/`
   and `Req/rust/baselines/` — so this port's numbers are comparable
   machine-to-machine and commit-to-commit the same way RZ's and Req's own
   are.

## Minimal usage sketch

```rust
use reqbench::{sample, DEFAULT_REPS, DEFAULT_WARMUP_MS};
use reqbench::mem::{CountingAlloc, cross_check, MemCrossCheck};
use reqbench::provenance::Provenance;
use reqbench::stats::{Record, load_baseline, compare, Verdict};

#[global_allocator]
static ALLOC: CountingAlloc = CountingAlloc::new();

fn main() {
    let prov = Provenance::capture();
    // TODO: replace with this port's own solve/verify call
    let samples = sample(DEFAULT_REPS, DEFAULT_WARMUP_MS, || {
        let _ = your_port::solve_under_test(&input, &nonce);
    });
    let rec = Record::from_samples("your_key_here", 1, &samples);
    println!("{}", rec.to_json_line("machine-tag", &prov));
}
```
