//! Bench reporting: records, robust statistics, JSON-lines emission, and
//! baseline comparison. Std-only by design (BENCHMARK.md §5 gap 3).
//!
//! Statistics: for a deterministic CPU-bound kernel, timing noise is strictly
//! additive, so the *minimum* estimates true cost; the median guards against
//! bimodality; the MAD is the within-run noise floor. Cross-process noise
//! (cache state, frequency, load) exceeds within-run MAD, so the comparison
//! band is max(MAD_new, MAD_baseline, band_pct% of baseline min) — measured
//! same-code cross-process deltas up to ~3% motivated the relative floor.
//! A delta counts only beyond that band. Resolution tiers: *within-process*
//! backend comparisons (the seam benches interleave candidates in one run)
//! resolve ~1%; *cross-process* baseline tracking resolves band_pct. Wins
//! smaller than the floor need interleaved A/B or a quieter machine, not a
//! smaller floor.

/// One measurement: a bench identity plus robust statistics over its samples.
#[derive(Clone, Debug)]
pub struct Record {
    /// Bench name, e.g. "leaf_hash", "solve/arena", "verify/reference".
    pub bench: String,
    pub n: u32,
    pub k: u32,
    /// Work units per sample run (leaves, iterations, ...), for per-unit math.
    pub units: u64,
    pub reps: usize,
    pub min_ns: u128,
    pub median_ns: u128,
    pub mad_ns: u128,
}

impl Record {
    /// Builds a record from raw per-rep samples (nanoseconds per run).
    pub fn from_samples(bench: &str, n: u32, k: u32, units: u64, samples: &[u128]) -> Record {
        let mut s = samples.to_vec();
        s.sort_unstable();
        let min = s[0];
        let median = s[s.len() / 2];
        let mut dev: Vec<u128> = s.iter().map(|&x| x.abs_diff(median)).collect();
        dev.sort_unstable();
        let mad = dev[dev.len() / 2];
        Record {
            bench: bench.into(),
            n,
            k,
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

    /// The identity a baseline entry is matched on.
    pub fn key(&self) -> String {
        format!("{}@({},{})", self.bench, self.n, self.k)
    }

    /// One JSON line, fixed key order, no escaping needed (names are
    /// identifier-like by construction).
    pub fn to_json_line(&self, tag: &str) -> String {
        format!(
            "{{\"bench\":\"{}\",\"n\":{},\"k\":{},\"units\":{},\"reps\":{},\
             \"min_ns\":{},\"median_ns\":{},\"mad_ns\":{},\"tag\":\"{}\",\"arch\":\"{}\"}}",
            self.bench,
            self.n,
            self.k,
            self.units,
            self.reps,
            self.min_ns,
            self.median_ns,
            self.mad_ns,
            tag,
            std::env::consts::ARCH,
        )
    }
}

/// Extracts `"field":<digits>` from a JSON line written by `to_json_line`.
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

/// Parses a baseline file (JSON lines as written by this module). Unknown or
/// malformed lines are skipped — a baseline is advisory, never a hard input.
/// A file may hold several runs appended; the baseline per key is the
/// per-run-min *median* across runs — a typical quiet run, so same-code
/// comparisons split symmetrically around it instead of sitting above a
/// best-ever floor.
pub fn load_baseline(text: &str) -> Vec<BaselineEntry> {
    let mut per_key: Vec<(String, Vec<u128>, Vec<u128>)> = Vec::new();
    for line in text.lines() {
        let Some((key, min_ns, mad_ns)) = (|| {
            Some((
                format!(
                    "{}@({},{})",
                    field_str(line, "bench")?,
                    field_u128(line, "n")?,
                    field_u128(line, "k")?
                ),
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

/// Comparison verdict per the decision rule.
pub enum Verdict {
    Win(f64),        // improvement, percent
    Regression(f64), // slowdown, percent
    Noise,
    New,
    /// Too few reps to estimate dispersion (single instrumented runs are
    /// informational — their ratios matter, their wall time is untracked).
    Untracked,
}

pub fn compare(rec: &Record, baseline: &[BaselineEntry], band_pct: u32) -> Verdict {
    if rec.reps < 3 {
        return Verdict::Untracked;
    }
    let Some(base) = baseline.iter().find(|b| b.key == rec.key()) else {
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

    #[test]
    fn stats_from_samples() {
        let r = Record::from_samples("x", 48, 5, 100, &[110, 100, 130, 105, 120]);
        assert_eq!(r.min_ns, 100);
        assert_eq!(r.median_ns, 110);
        assert_eq!(r.mad_ns, 10); // |{110,100,130,105,120} - 110| = {0,10,20,5,10}
        assert_eq!(r.reps, 5);
    }

    #[test]
    fn json_roundtrip_and_compare() {
        let r = Record::from_samples("leaf_hash", 48, 5, 1 << 17, &[1000, 1010, 1005]);
        let line = r.to_json_line("test-machine");
        let base = load_baseline(&line);
        assert_eq!(base.len(), 1);
        assert_eq!(base[0].key, "leaf_hash@(48,5)");
        assert_eq!(base[0].min_ns, 1000);
        // Same numbers: inside the band -> noise.
        assert!(matches!(compare(&r, &base, 2), Verdict::Noise));
        // A clear win beyond the band (>2% floor and > MADs).
        let fast = Record::from_samples("leaf_hash", 48, 5, 1 << 17, &[900, 902, 905]);
        assert!(matches!(compare(&fast, &base, 2), Verdict::Win(_)));
        // A small (1%) improvement stays inside the relative floor -> noise.
        let slight = Record::from_samples("leaf_hash", 48, 5, 1 << 17, &[990, 991, 992]);
        assert!(matches!(compare(&slight, &base, 2), Verdict::Noise));
        // Unknown bench -> new.
        let other = Record::from_samples("verify", 48, 5, 1, &[5, 5, 5]);
        assert!(matches!(compare(&other, &base, 2), Verdict::New));
        // Single-rep records are never tracked.
        let one = Record::from_samples("leaf_hash", 48, 5, 1 << 17, &[950]);
        assert!(matches!(compare(&one, &base, 2), Verdict::Untracked));
    }
}
