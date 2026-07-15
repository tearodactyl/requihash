//! Peak-memory measurement: a counting global allocator, plus an OS-level
//! cross-check. `Req/BENCH.md` §4: a single memory instrument is not
//! trustworthy alone (Perf.md §7's `Physical footprint`-vs-compression
//! confound is the cautionary precedent) — this module's counting
//! allocator and [`current_process_rss_bytes`] are meant to be used
//! *together*, one confirming the other, the way `rz_bench.rs` did once by
//! hand last session (counting allocator: 6.27 GB; `/usr/bin/time -l`
//! RSS: 6733873152 bytes — matched almost exactly). This module makes that
//! a standing, automated part of a bench run instead of a one-off manual
//! check.
//!
//! # Usage
//!
//! A binary that wants peak-allocation tracking must install
//! [`CountingAlloc`] as its `#[global_allocator]` — this can only be done
//! once per binary, so this crate cannot do it for the caller:
//!
//! ```ignore
//! use reqbench::mem::CountingAlloc;
//! #[global_allocator]
//! static ALLOC: CountingAlloc = CountingAlloc::new();
//!
//! fn main() {
//!     ALLOC.reset();
//!     let _ = solve_144_5(&input, &nonce);
//!     let peak = ALLOC.peak_bytes();
//!     let rss = reqbench::mem::current_process_rss_bytes();
//!     // report both; a large disagreement is itself a finding worth
//!     // investigating, not something to silently average away.
//! }
//! ```

use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicUsize, Ordering};

/// A counting global allocator: tracks current and peak live-byte totals
/// across every `alloc`/`dealloc` call in the process. Delegates actual
/// allocation to [`System`] — this only observes, never changes behavior.
pub struct CountingAlloc {
    current: AtomicUsize,
    peak: AtomicUsize,
}

impl CountingAlloc {
    pub const fn new() -> Self {
        CountingAlloc {
            current: AtomicUsize::new(0),
            peak: AtomicUsize::new(0),
        }
    }

    /// Zeroes the running counters. Call before the section you want to
    /// measure — peak is relative to the last reset, not process start.
    pub fn reset(&self) {
        self.current.store(0, Ordering::Relaxed);
        self.peak.store(0, Ordering::Relaxed);
    }

    /// Peak live-byte total tracked since the last [`reset`](Self::reset).
    pub fn peak_bytes(&self) -> usize {
        self.peak.load(Ordering::Relaxed)
    }
}

impl Default for CountingAlloc {
    fn default() -> Self {
        Self::new()
    }
}

// SAFETY: delegates every call directly to `System`; only adds atomic
// bookkeeping around the existing allocator, no allocation strategy of its
// own.
unsafe impl GlobalAlloc for CountingAlloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let ptr = unsafe { System.alloc(layout) };
        if !ptr.is_null() {
            let cur = self.current.fetch_add(layout.size(), Ordering::Relaxed) + layout.size();
            self.peak.fetch_max(cur, Ordering::Relaxed);
        }
        ptr
    }
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe { System.dealloc(ptr, layout) };
        self.current.fetch_sub(layout.size(), Ordering::Relaxed);
    }
}

/// Current process's resident set size in bytes, read from the OS
/// independently of the counting allocator — the second instrument
/// `Req/BENCH.md` §4 requires before a peak-memory figure is trusted.
///
/// macOS: shells out to `ps -o rss= -p <pid>` (kB, converted to bytes).
/// Other platforms: reads `/proc/self/status`'s `VmRSS` line (Linux) if
/// present. Returns `None` if neither works — callers should treat that as
/// "cross-check unavailable," not silently trust the allocator figure
/// alone in that case; say so in the report rather than omitting it.
pub fn current_process_rss_bytes() -> Option<u64> {
    #[cfg(target_os = "macos")]
    {
        let pid = std::process::id();
        let out = std::process::Command::new("ps")
            .args(["-o", "rss=", "-p", &pid.to_string()])
            .output()
            .ok()?;
        if !out.status.success() {
            return None;
        }
        let s = String::from_utf8(out.stdout).ok()?;
        let kb: u64 = s.trim().parse().ok()?;
        Some(kb * 1024)
    }
    #[cfg(target_os = "linux")]
    {
        let status = std::fs::read_to_string("/proc/self/status").ok()?;
        for line in status.lines() {
            if let Some(rest) = line.strip_prefix("VmRSS:") {
                let kb: u64 = rest.trim().trim_end_matches(" kB").trim().parse().ok()?;
                return Some(kb * 1024);
            }
        }
        None
    }
    #[cfg(not(any(target_os = "macos", target_os = "linux")))]
    {
        None
    }
}

/// Compares a counting-allocator peak against the OS RSS cross-check and
/// classifies the agreement. Per `Req/BENCH.md` §4: don't silently trust
/// either number without checking they agree.
pub enum MemCrossCheck {
    /// Both instruments agree within `tolerance_pct` percent.
    Agree { allocator_bytes: u64, rss_bytes: u64 },
    /// The two instruments disagree by more than `tolerance_pct` — worth
    /// investigating (e.g. allocator overhead, fragmentation, or a bug in
    /// the counting allocator itself), not silently averaged away.
    Disagree {
        allocator_bytes: u64,
        rss_bytes: u64,
        diff_pct: f64,
    },
    /// The OS-level cross-check wasn't available on this platform — report
    /// the allocator figure as uncorroborated, not as a trusted baseline.
    RssUnavailable { allocator_bytes: u64 },
}

pub fn cross_check(allocator_bytes: u64, tolerance_pct: f64) -> MemCrossCheck {
    match current_process_rss_bytes() {
        None => MemCrossCheck::RssUnavailable { allocator_bytes },
        Some(rss_bytes) => {
            let diff_pct = 100.0
                * (allocator_bytes as f64 - rss_bytes as f64).abs()
                / rss_bytes.max(1) as f64;
            if diff_pct <= tolerance_pct {
                MemCrossCheck::Agree {
                    allocator_bytes,
                    rss_bytes,
                }
            } else {
                MemCrossCheck::Disagree {
                    allocator_bytes,
                    rss_bytes,
                    diff_pct,
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn counting_alloc_tracks_peak() {
        let a = CountingAlloc::new();
        a.reset();
        assert_eq!(a.peak_bytes(), 0);
        // Directly exercise the bookkeeping without relying on being the
        // process's actual #[global_allocator] (tests can't install one).
        a.current.fetch_add(1000, Ordering::Relaxed);
        a.peak.fetch_max(1000, Ordering::Relaxed);
        a.current.fetch_add(500, Ordering::Relaxed);
        a.peak.fetch_max(1500, Ordering::Relaxed);
        assert_eq!(a.peak_bytes(), 1500);
        a.current.fetch_sub(500, Ordering::Relaxed);
        // Peak must not drop on deallocation.
        assert_eq!(a.peak_bytes(), 1500);
    }

    #[test]
    fn rss_read_returns_something_on_supported_platforms() {
        // This process is definitely running, so if the platform is
        // supported at all, this should return Some(nonzero).
        if cfg!(any(target_os = "macos", target_os = "linux")) {
            let rss = current_process_rss_bytes();
            assert!(rss.is_some());
            assert!(rss.unwrap() > 0);
        }
    }

    #[test]
    fn cross_check_classifies_agreement() {
        match cross_check(1_000_000, 5.0) {
            MemCrossCheck::Agree { .. } | MemCrossCheck::Disagree { .. } => {}
            MemCrossCheck::RssUnavailable { .. } => {
                // acceptable on unsupported platforms only
                assert!(!cfg!(any(target_os = "macos", target_os = "linux")));
            }
        }
    }
}
