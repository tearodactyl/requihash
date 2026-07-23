//! Unified cross-implementation run-record schema and file-naming
//! convention. One JSON-lines shape every measurement in
//! `Req/SOLVER_CORPUS/` converges on, so a run from `rk` (Rust), `cs`
//! V1-V6 (C++), and Req itself is directly comparable field-for-field
//! without reconciling different printf/table shapes by hand.
//!
//! **No implementation ever writes another implementation's file.** Each
//! implementation gets its own `runs/` directory
//! (`Req/SOLVER_CORPUS/<impl>/runs/`), and every invocation creates a
//! NEW, uniquely timestamped file there — never appends to a shared file,
//! never overwrites a prior run. This sidesteps the concurrent-write
//! question entirely (there is no file two processes could contend for)
//! and directly answers "how do I know I'm not comparing against an
//! obsolete run": the directory listing IS the run history, sorted by
//! filename (a `YYYYMMDDTHHMMSSZ` UTC timestamp, dash-free, so it sorts
//! lexicographically = chronologically), and every record additionally
//! carries `git_rev`/`git_dirty` so a specific run's code state is never
//! ambiguous even if two runs share a timestamp-adjacent moment.
//! Cross-implementation synthesis (the comparison table, the ladder
//! summary) is a SEPARATE, later activity that globs every impl's
//! `runs/*.jsonl` and reads them — it does not write back into any of
//! them.
//!
//! Distinct from [`crate::stats::Record`] (that type is the
//! baseline-comparison record — `key`/`units`/timing only, tuned for the
//! Win/Regression/Noise workflow against a prior `baselines/*.jsonl`
//! file, a DIFFERENT purpose: regression tracking within one
//! implementation over time, not cross-implementation comparison) —
//! `RunRecord` is the broader, ad hoc measurement-campaign record: it
//! carries the (n,k)-vs-(n,K) convention tag explicitly (this corpus's
//! own recurring source of confusion — Requihash's tree-depth `k` where
//! solution size is `2^k`, vs. Sequihash/CS's list-count `K` matching the
//! paper's own `(n,K=2^k)` table are NOT the same number for the same
//! algorithm instance, see `Req/README.md`'s "What Requihash changes" and
//! `cs/README.md`'s own two-conventions note) and a memory reading with
//! its instrument named, not just a bare byte count.
//!
//! C++ drivers have no dependency on this crate (no shared C++ library
//! across `SOLVER_CORPUS` ports, by design — see `SOLVER_CORPUS.md`'s
//! standalone-port requirement) — they emit the identical field set and
//! the identical `runs/<timestamp>.jsonl` naming convention by hand,
//! matching [`RunRecord::to_json_line`]'s exact key order/types and
//! [`run_filename`]'s exact timestamp format.

/// Which index-count convention `point`'s `k_or_k` field uses. Kept as an
/// explicit tag rather than inferred from the implementation name — a
/// reader of `RUN_DATA.jsonl` should never have to know "CS uses
/// list-count" as tribal knowledge to parse a row correctly.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum KConvention {
    /// Requihash/Equihash tree-depth: solution has `2^k` indices.
    TreeDepth,
    /// Sequihash/paper/CS list-count: solution has `K` indices directly
    /// (`K` a power of 2, matching the paper's own `(n, K=2^k)` table).
    ListCount,
}

impl KConvention {
    fn as_str(&self) -> &'static str {
        match self {
            KConvention::TreeDepth => "tree_depth_k",
            KConvention::ListCount => "list_count_K",
        }
    }
}

/// Which instrument produced `peak_mem_bytes`. Never mix these across a
/// comparison without saying so (`Req/BENCH.md` §4's two-instrument
/// discipline; see `Req/SOLVER_CORPUS/rk/README.md`'s corrected
/// C++-vs-Rust comparison for what happens when this isn't tracked
/// explicitly).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MemInstrument {
    /// Counting global allocator: live bytes requested, peak. (Rust
    /// `reqbench::mem::CountingAlloc`, or an equivalent C/C++ instrumented
    /// allocator.)
    AllocatorPeak,
    /// OS-reported resident set size (Mach `task_info` on macOS,
    /// `/proc/self/status` `VmHWM` on Linux, `/usr/bin/time -l` on the
    /// command line). Physical pages actually mapped, not requested
    /// bytes — related but not identical to allocator peak (allocator
    /// bookkeeping/page-rounding overhead sits on top).
    OsRss,
    /// No memory reading taken this run (timing-only record).
    None,
}

impl MemInstrument {
    fn as_str(&self) -> &'static str {
        match self {
            MemInstrument::AllocatorPeak => "allocator_peak",
            MemInstrument::OsRss => "os_rss",
            MemInstrument::None => "none",
        }
    }
}

/// One row of `RUN_DATA.jsonl`.
#[derive(Clone, Debug)]
pub struct RunRecord {
    /// Implementation identity, e.g. `"rk-rust"`, `"rk-cpp"`, `"cs-v4"`,
    /// `"cs-base"`, `"req-rust"`, `"req-cpp"`, `"eq-two-level"`. Free-form
    /// but should match the directory/binary name a reader could go look
    /// at, not an abbreviation only this run's author would recognize.
    pub impl_name: String,
    /// `"rust"` or `"cpp"` — kept separate from `impl_name` so filtering/
    /// grouping by language doesn't need string-parsing `impl_name`.
    pub lang: String,
    /// The `n` parameter (bits), common to every convention this corpus uses.
    pub n: u32,
    /// The `k`-or-`K` value AS WRITTEN IN THE COMMAND LINE / CALL SITE for
    /// this run — paired with `k_convention` so it's unambiguous which
    /// quantity this is. Do not convert between conventions before
    /// storing; store what the implementation was actually invoked with.
    pub k_or_k: u32,
    pub k_convention: KConvention,
    /// Nonce or seed identity, as a hex string (nonce) or decimal string
    /// (seed) — whichever the implementation actually takes. Free-form
    /// text, not parsed, but must be the literal value used so a run is
    /// reproducible from this row alone.
    pub nonce_or_seed: String,
    pub reps: usize,
    pub wall_min_ms: f64,
    pub wall_median_ms: f64,
    pub wall_mad_ms: f64,
    /// `None` if this row is timing-only.
    pub peak_mem_bytes: Option<u64>,
    pub mem_instrument: MemInstrument,
    /// Solutions found at this (point, nonce) — not a correctness claim
    /// (a differential suite is what proves correctness), just recorded
    /// so a reader can sanity-check "did this run find anything" without
    /// re-running it.
    pub solutions_found: Option<u64>,
    pub git_rev: String,
    pub git_dirty: bool,
    /// Free-form machine identity, e.g. `"Apple M4 Pro, macOS 26.3,
    /// arm64, 14 cores"` — `Req/BENCH.md` §1's machine-identity field,
    /// deliberately a string here (not structured) since the set of
    /// fields that matter differs by platform and forcing a schema on it
    /// would just produce mostly-null columns.
    pub machine: String,
}

impl RunRecord {
    /// One JSON line, field order and types fixed here as the contract a
    /// C++ driver's hand-written `printf` must match exactly (field
    /// names, order, and JSON types — object for `point`, null for
    /// missing optionals, not omission of the key). Written to that
    /// implementation's own `runs/<timestamp>.jsonl` (see [`run_filename`]),
    /// never appended to another implementation's file.
    pub fn to_json_line(&self) -> String {
        let mem_field = match self.peak_mem_bytes {
            Some(b) => b.to_string(),
            None => "null".to_string(),
        };
        let sol_field = match self.solutions_found {
            Some(s) => s.to_string(),
            None => "null".to_string(),
        };
        format!(
            "{{\"impl\":{},\"lang\":{},\"point\":{{\"n\":{},\"k_or_K\":{},\"convention\":{}}},\
             \"nonce_or_seed\":{},\"reps\":{},\"wall_min_ms\":{:.4},\"wall_median_ms\":{:.4},\
             \"wall_mad_ms\":{:.4},\"peak_mem_bytes\":{},\"mem_instrument\":{},\
             \"solutions_found\":{},\"git_rev\":{},\"git_dirty\":{},\"machine\":{}}}",
            json_str(&self.impl_name),
            json_str(&self.lang),
            self.n,
            self.k_or_k,
            json_str(self.k_convention.as_str()),
            json_str(&self.nonce_or_seed),
            self.reps,
            self.wall_min_ms,
            self.wall_median_ms,
            self.wall_mad_ms,
            mem_field,
            json_str(self.mem_instrument.as_str()),
            sol_field,
            json_str(&self.git_rev),
            self.git_dirty,
            json_str(&self.machine),
        )
    }
}

/// The shared file-naming convention: `<n><k_or_K>_<UTC-timestamp>.jsonl`
/// -- e.g. `604_20260721T195949Z.jsonl` for (n=60, k=4) run at that
/// instant. The point prefix isn't cosmetic: a fast sequential sweep
/// calls this within the same wall-clock second for DIFFERENT points
/// (caught in practice — an (n=60,k=4) run immediately followed by
/// (n=90,k=5) landed in the same second and would have collided on a
/// bare timestamp), so the point must be part of the filename, not just
/// inside the JSON payload. Timestamp itself is `YYYYMMDDTHHMMSSZ`
/// (dash-free, so the timestamp portion still sorts lexicographically =
/// chronologically within one point; `Z` suffix makes the UTC-ness
/// explicit rather than assumed). Every implementation's driver creates
/// exactly one NEW file per invocation at
/// `Req/SOLVER_CORPUS/<impl>/runs/<this>.jsonl` -- never opens an
/// existing file (an actual same-point-same-second rerun still collides
/// and is meant to: `create_new`/`O_EXCL` fails loudly rather than
/// silently overwriting; rerun a second later). A C++ driver must
/// reproduce this exact format (see this module's own doc comment).
pub fn run_filename(n: u32, k_or_k: u32, now: std::time::SystemTime) -> String {
    let secs = now
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    let (y, mo, d, h, mi, s) = civil_from_unix(secs as i64);
    format!("{n}{k_or_k}_{y:04}{mo:02}{d:02}T{h:02}{mi:02}{s:02}Z.jsonl")
}

/// Converts a Unix timestamp to UTC calendar fields, no chrono/time
/// dependency (this crate is std-only by design, `SOLVER_CORPUS.md`'s own
/// standalone-port requirement). Howard Hinnant's well-known
/// days-from-civil algorithm, run in reverse; correct for the entire
/// proleptic Gregorian calendar, not just some plausible-looking range.
fn civil_from_unix(unix_secs: i64) -> (i64, u32, u32, u32, u32, u32) {
    let days = unix_secs.div_euclid(86400);
    let secs_of_day = unix_secs.rem_euclid(86400);
    let (h, mi, s) = (secs_of_day / 3600, (secs_of_day / 60) % 60, secs_of_day % 60);

    let z = days + 719468;
    let era = if z >= 0 { z } else { z - 146096 } / 146097;
    let doe = (z - era * 146097) as u64; // [0, 146096]
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
    let y = yoe as i64 + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100); // [0, 365]
    let mp = (5 * doy + 2) / 153; // [0, 11]
    let d = (doy - (153 * mp + 2) / 5 + 1) as u32; // [1, 31]
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as u32; // [1, 12]
    let year = if m <= 2 { y + 1 } else { y };
    (year, m, d, h as u32, mi as u32, s as u32)
}

fn json_str(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 2);
    out.push('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            _ => out.push(c),
        }
    }
    out.push('"');
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn json_line_has_expected_shape() {
        let r = RunRecord {
            impl_name: "rk-rust".into(),
            lang: "rust".into(),
            n: 108,
            k_or_k: 5,
            k_convention: KConvention::TreeDepth,
            nonce_or_seed: "5".into(),
            reps: 3,
            wall_min_ms: 351.95,
            wall_median_ms: 360.0,
            wall_mad_ms: 5.0,
            peak_mem_bytes: Some(223_019_008),
            mem_instrument: MemInstrument::OsRss,
            solutions_found: Some(1),
            git_rev: "987c16b".into(),
            git_dirty: false,
            machine: "Apple M4 Pro".into(),
        };
        let line = r.to_json_line();
        assert!(line.contains("\"impl\":\"rk-rust\""));
        assert!(line.contains("\"convention\":\"tree_depth_k\""));
        assert!(line.contains("\"mem_instrument\":\"os_rss\""));
        assert!(line.contains("\"peak_mem_bytes\":223019008"));
    }

    #[test]
    fn run_filename_matches_known_utc_instant() {
        // 2026-07-21T18:53:11Z == unix 1784659991 (cross-checked against
        // Python's datetime, an independent implementation).
        let t = std::time::UNIX_EPOCH + std::time::Duration::from_secs(1784659991);
        assert_eq!(run_filename(60, 4, t), "604_20260721T185311Z.jsonl");
    }

    #[test]
    fn run_filename_matches_unix_epoch() {
        assert_eq!(run_filename(0, 0, std::time::UNIX_EPOCH), "00_19700101T000000Z.jsonl");
    }

    #[test]
    fn run_filename_sorts_chronologically_within_one_point() {
        let a = run_filename(60, 4, std::time::UNIX_EPOCH + std::time::Duration::from_secs(1_700_000_000));
        let b = run_filename(60, 4, std::time::UNIX_EPOCH + std::time::Duration::from_secs(1_800_000_000));
        assert!(a < b);
    }

    #[test]
    fn run_filename_disambiguates_different_points_in_the_same_second() {
        // The real collision this fix addresses: (60,4) then (90,5)
        // invoked in the same wall-clock second must not produce the
        // same filename.
        let same_instant = std::time::UNIX_EPOCH + std::time::Duration::from_secs(1_700_000_000);
        let a = run_filename(60, 4, same_instant);
        let b = run_filename(90, 5, same_instant);
        assert_ne!(a, b);
    }
}
