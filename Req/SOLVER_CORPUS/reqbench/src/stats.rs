//! Timing statistics, JSON-lines emission, and baseline comparison.
//! Std-only by design — no external bench framework, no dependencies, so
//! any standalone port under `Req/SOLVER_CORPUS/` can depend on this crate
//! without pulling in anything heavier.
//!
//! This is a generalized extraction of `Req/rust/src/report.rs`'s
//! `Record`/`BaselineEntry`/`compare`/`Verdict` (see `Req/BENCH.md` §7 for
//! why and `report.rs`'s own doc comment for the statistical reasoning,
//! reproduced below since a standalone port has no reason to read the
//! `requihash` crate to understand its own bench harness).
//!
//! Statistics: for a deterministic CPU-bound kernel, timing noise is
//! strictly additive, so the *minimum* sample estimates true cost; the
//! median guards against bimodality; the MAD (median absolute deviation)
//! is the within-run noise floor. Cross-process noise (cache state,
//! frequency, thermal/load state) exceeds within-run MAD on an unpinned
//! machine — measured same-code cross-process deltas up to ~3-7% in this
//! project's own history — so the comparison band is
//! `max(MAD_new, MAD_baseline, band_pct% of baseline_min)`. A delta counts
//! only beyond that band. See `Req/BENCH.md` §3.
//!
//! The one generalization versus `report.rs`: the identity key is a plain
//! `String` the caller builds (e.g. `"solve_144_5"`, or
//! `"solve@(WN=144,WK=5,RESTBITS=4)"`), not a hardcoded `(n,k)` pair — a
//! `SOLVER_CORPUS` port's natural key is `(WN,WK,RESTBITS)`, not Equihash's
//! `(n,k)`, and forcing one project's key shape onto every port would be
//! exactly the kind of coupling `SOLVER_CORPUS.md`'s own cross-cutting
//! requirements warn against ("no other context needed from this
//! repository's other documents").

/// One measurement: an identity key plus robust statistics over its
/// samples, and the provenance (§`Req/BENCH.md` §1) needed to make the
/// number traceable later.
#[derive(Clone, Debug)]
pub struct Record {
    /// Caller-built identity, e.g. `"solve_144_5"` or a `key!()`-built
    /// string. Two records with the same `key` are treated as the same
    /// benchmark across runs for baseline comparison.
    pub key: String,
    /// Work units per sample run (leaves, solves, iterations, ...), for
    /// per-unit math. Use 1 if there is no natural unit.
    pub units: u64,
    pub reps: usize,
    pub min_ns: u128,
    pub median_ns: u128,
    pub mad_ns: u128,
}

impl Record {
    /// Builds a record from raw per-rep samples (nanoseconds per run).
    /// Panics if `samples` is empty — a record with zero data points isn't
    /// a measurement.
    pub fn from_samples(key: impl Into<String>, units: u64, samples: &[u128]) -> Record {
        assert!(!samples.is_empty(), "Record::from_samples: no samples");
        let mut s = samples.to_vec();
        s.sort_unstable();
        let min = s[0];
        let median = s[s.len() / 2];
        let mut dev: Vec<u128> = s.iter().map(|&x| x.abs_diff(median)).collect();
        dev.sort_unstable();
        let mad = dev[dev.len() / 2];
        Record {
            key: key.into(),
            units,
            reps: samples.len(),
            min_ns: min,
            median_ns: median,
            mad_ns: mad,
        }
    }

    /// Nanoseconds per work unit, from the minimum.
    pub fn per_unit_ns(&self) -> f64 {
        self.min_ns as f64 / self.units.max(1) as f64
    }

    /// One JSON line. Includes provenance fields per `Req/BENCH.md` §1 —
    /// this is the field this crate adds over `report.rs`'s original,
    /// which had no git-identity field (`Req/BENCH.md` §6).
    pub fn to_json_line(&self, tag: &str, prov: &crate::provenance::Provenance) -> String {
        format!(
            "{{\"key\":{},\"units\":{},\"reps\":{},\
             \"min_ns\":{},\"median_ns\":{},\"mad_ns\":{},\"tag\":{},\"arch\":{},\
             \"git_rev\":{},\"git_dirty\":{},\"profile\":{}}}",
            json_str(&self.key),
            self.units,
            self.reps,
            self.min_ns,
            self.median_ns,
            self.mad_ns,
            json_str(tag),
            json_str(std::env::consts::ARCH),
            json_str(&prov.git_rev),
            prov.git_dirty,
            json_str(&prov.profile),
        )
    }
}

/// Minimal JSON string escaping — only what identifier-like keys and short
/// tags actually need (quote and backslash); this crate has no JSON
/// dependency by design.
fn json_str(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 2);
    out.push('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            _ => out.push(c),
        }
    }
    out.push('"');
    out
}

fn field_u128(line: &str, field: &str) -> Option<u128> {
    let pat = format!("\"{field}\":");
    let at = line.find(&pat)? + pat.len();
    let rest = &line[at..];
    let end = rest
        .find(|c: char| !c.is_ascii_digit())
        .unwrap_or(rest.len());
    rest[..end].parse().ok()
}

fn field_str<'a>(line: &'a str, field: &str) -> Option<&'a str> {
    let pat = format!("\"{field}\":\"");
    let at = line.find(&pat)? + pat.len();
    let rest = &line[at..];
    Some(&rest[..rest.find('"')?])
}

/// A baseline entry parsed back from a JSON line.
pub struct BaselineEntry {
    pub key: String,
    pub min_ns: u128,
    pub mad_ns: u128,
}

/// Parses a baseline file (JSON lines as written by [`Record::to_json_line`]).
/// Unknown or malformed lines are skipped — a baseline is advisory, never a
/// hard input. A file may hold several runs appended; the baseline per key
/// is the per-run-min *median* across runs — a typical quiet run, so
/// same-code comparisons split symmetrically around it instead of sitting
/// above a best-ever floor (same reasoning as `report.rs`'s original).
pub fn load_baseline(text: &str) -> Vec<BaselineEntry> {
    let mut per_key: Vec<(String, Vec<u128>, Vec<u128>)> = Vec::new();
    for line in text.lines() {
        let Some((key, min_ns, mad_ns)) = (|| {
            Some((
                field_str(line, "key")?.to_string(),
                field_u128(line, "min_ns")?,
                field_u128(line, "mad_ns")?,
            ))
        })() else {
            continue;
        };
        match per_key.iter_mut().find(|(k, _, _)| *k == key) {
            Some((_, mins, mads)) => {
                mins.push(min_ns);
                mads.push(mad_ns);
            }
            None => per_key.push((key, vec![min_ns], vec![mad_ns])),
        }
    }
    per_key
        .into_iter()
        .map(|(key, mut mins, mut mads)| {
            mins.sort_unstable();
            mads.sort_unstable();
            BaselineEntry {
                key,
                min_ns: mins[mins.len() / 2],
                mad_ns: mads[mads.len() / 2],
            }
        })
        .collect()
}

/// Comparison verdict per the decision rule (`Req/BENCH.md` §3). Never
/// report a bare percentage without going through this — a percentage
/// without a stated noise floor invites over-reading a number that's
/// actually noise.
pub enum Verdict {
    Win(f64),        // improvement, percent
    Regression(f64), // slowdown, percent
    Noise,
    New,
    /// Too few reps to estimate dispersion. Single-sample records are
    /// informational only — see `Req/BENCH.md` §2, do not report these as
    /// baselines.
    Untracked,
}

pub fn compare(rec: &Record, baseline: &[BaselineEntry], band_pct: u32) -> Verdict {
    if rec.reps < 3 {
        return Verdict::Untracked;
    }
    let Some(base) = baseline.iter().find(|b| b.key == rec.key) else {
        return Verdict::New;
    };
    let floor = base.min_ns * band_pct as u128 / 100;
    let band = rec.mad_ns.max(base.mad_ns).max(floor);
    if rec.min_ns.abs_diff(base.min_ns) <= band {
        return Verdict::Noise;
    }
    let pct = 100.0 * (base.min_ns as f64 - rec.min_ns as f64) / base.min_ns as f64;
    if rec.min_ns < base.min_ns {
        Verdict::Win(pct)
    } else {
        Verdict::Regression(-pct)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::provenance::Provenance;

    fn dummy_prov() -> Provenance {
        Provenance {
            git_rev: "deadbeef".into(),
            git_dirty: false,
            profile: "release".into(),
        }
    }

    #[test]
    fn stats_from_samples() {
        let r = Record::from_samples("x", 100, &[110, 100, 130, 105, 120]);
        assert_eq!(r.min_ns, 100);
        assert_eq!(r.median_ns, 110);
        assert_eq!(r.mad_ns, 10); // |{110,100,130,105,120} - 110| = {0,10,20,5,10}
        assert_eq!(r.reps, 5);
    }

    #[test]
    #[should_panic(expected = "no samples")]
    fn empty_samples_panics() {
        Record::from_samples("x", 1, &[]);
    }

    #[test]
    fn json_roundtrip_and_compare() {
        let r = Record::from_samples("solve_144_5", 1 << 17, &[1000, 1010, 1005]);
        let line = r.to_json_line("test-machine", &dummy_prov());
        assert!(line.contains("\"git_rev\":\"deadbeef\""));
        assert!(line.contains("\"git_dirty\":false"));
        let base = load_baseline(&line);
        assert_eq!(base.len(), 1);
        assert_eq!(base[0].key, "solve_144_5");
        assert_eq!(base[0].min_ns, 1000);
        // Same numbers: inside the band -> noise.
        assert!(matches!(compare(&r, &base, 2), Verdict::Noise));
        // A clear win beyond the band (>2% floor and > MADs).
        let fast = Record::from_samples("solve_144_5", 1 << 17, &[900, 902, 905]);
        assert!(matches!(compare(&fast, &base, 2), Verdict::Win(_)));
        // A small (1%) improvement stays inside the relative floor -> noise.
        let slight = Record::from_samples("solve_144_5", 1 << 17, &[990, 991, 992]);
        assert!(matches!(compare(&slight, &base, 2), Verdict::Noise));
        // Unknown key -> new.
        let other = Record::from_samples("verify_144_5", 1, &[5, 5, 5]);
        assert!(matches!(compare(&other, &base, 2), Verdict::New));
        // Single-rep records are never tracked.
        let one = Record::from_samples("solve_144_5", 1 << 17, &[950]);
        assert!(matches!(compare(&one, &base, 2), Verdict::Untracked));
    }

    #[test]
    fn json_escaping() {
        assert_eq!(json_str("plain"), "\"plain\"");
        assert_eq!(json_str("with\"quote"), "\"with\\\"quote\"");
    }
}
