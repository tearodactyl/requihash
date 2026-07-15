//! Git-identity stamping for bench records. `Req/BENCH.md` §1: every
//! reported number should be traceable back to exactly what produced it —
//! this is the piece `Req/rust/src/report.rs` didn't have (`Req/BENCH.md`
//! §6), added here rather than silently retrofitted into that module.

/// Git commit + dirty-tree flag, plus the build profile, at the point a
/// bench binary ran. Shells out to `git` at runtime (not build time via
/// `build.rs`) so a `cargo run` without a rebuild still reports the
/// *current* tree state, not a stale value baked in at last compile.
pub struct Provenance {
    /// Short git commit hash of `HEAD`, or `"unknown"` if `git` isn't
    /// available or this isn't a git checkout (e.g. a tarball extraction).
    pub git_rev: String,
    /// True if the working tree has uncommitted changes at the time the
    /// binary ran. A "dirty" result is still worth recording — it just
    /// means the number can't be reproduced from the commit hash alone.
    pub git_dirty: bool,
    /// `"release"` or `"debug"`, from `cfg!(debug_assertions)` — not from
    /// a guess, so a debug-build number can never be silently mistaken for
    /// a release one (a real gap RZ's own STATUS.md had to caveat by hand
    /// once; see `Req/BENCH.md` §1).
    pub profile: String,
}

impl Provenance {
    /// Captures current git identity by shelling out (`git rev-parse`,
    /// `git status --porcelain`) from the current working directory.
    /// Never panics: any failure (no git binary, not a repo) becomes
    /// `git_rev = "unknown"`, `git_dirty = false` rather than aborting a
    /// bench run over a missing provenance field.
    pub fn capture() -> Provenance {
        let git_rev = run_git(&["rev-parse", "--short", "HEAD"])
            .unwrap_or_else(|| "unknown".to_string());
        let git_dirty = run_git(&["status", "--porcelain"])
            .map(|s| !s.trim().is_empty())
            .unwrap_or(false);
        let profile = if cfg!(debug_assertions) {
            "debug"
        } else {
            "release"
        }
        .to_string();
        Provenance {
            git_rev,
            git_dirty,
            profile,
        }
    }
}

fn run_git(args: &[&str]) -> Option<String> {
    let out = std::process::Command::new("git").args(args).output().ok()?;
    if !out.status.success() {
        return None;
    }
    let s = String::from_utf8(out.stdout).ok()?;
    let trimmed = s.trim();
    if trimmed.is_empty() {
        None
    } else {
        Some(trimmed.to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn capture_never_panics() {
        // Whatever the environment, this must return something usable.
        let p = Provenance::capture();
        assert!(!p.git_rev.is_empty());
        assert!(p.profile == "debug" || p.profile == "release");
    }
}
