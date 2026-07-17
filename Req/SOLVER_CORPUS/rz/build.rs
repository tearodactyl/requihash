//! Builds three cross-check binaries, one per (WN, RESTBITS) pair that
//! actually compiles in the vendored, single-core-stripped
//! `equihash-0.3.0/tromp/equi_miner.c` (see README.md for how that set was
//! derived from the file's own `#if`/`#elif`/`#error` branches).
//!
//! Each binary links the vendored `equi_miner.c` (found via glob under
//! `~/.cargo/registry`, unmodified, not copied) together with
//! `cross_check_c/harness_main.c` (harness code written for this port) and
//! the reference BLAKE2b implementation at
//! `~/Work/ZK/ZKs/BLAKE/blake2-reference/ref/blake2b-ref.c` (Samuel Neves, CC0 --
//! a separate, unmodified reference crypto implementation, not the pinned
//! equihash-0.3.0 source).
//!
//! This is a build-time cross-check binary generator, not part of the
//! `rz` library itself -- `cargo build`/`cargo test` produce these binaries
//! as a side effect so `tests/cross_check.rs` and `src/bin/rz_gen.rs` can
//! invoke them as subprocesses.

use std::env;
use std::path::{Path, PathBuf};

struct ParamSet {
    wn: u32,
    wk: u32,
    restbits: u32,
}

const PARAM_SETS: &[ParamSet] = &[
    ParamSet { wn: 200, wk: 9, restbits: 9 },
    ParamSet { wn: 200, wk: 9, restbits: 8 },
    ParamSet { wn: 144, wk: 5, restbits: 4 },
];

fn find_vendored_equi_miner_dir() -> PathBuf {
    if let Ok(over) = env::var("RZ_EQUIHASH_TROMP_DIR") {
        return PathBuf::from(over);
    }
    let home = env::var("HOME").expect("HOME not set");
    let pattern = format!("{home}/.cargo/registry/src/index.crates.io-*/equihash-0.3.0/tromp");
    let mut matches: Vec<PathBuf> = glob_paths(&pattern);
    matches.sort();
    matches
        .into_iter()
        .next()
        .unwrap_or_else(|| panic!("could not resolve vendored tromp/ dir via glob: {pattern}"))
}

/// Minimal single-star glob over one path segment (no external glob crate
/// dependency needed for this one lookup).
fn glob_paths(pattern: &str) -> Vec<PathBuf> {
    let star_pos = pattern.find('*').expect("pattern must contain one '*'");
    let prefix = &pattern[..star_pos];
    let suffix = &pattern[star_pos + 1..];
    let prefix_path = Path::new(prefix);
    let (parent, file_prefix) = if prefix.ends_with('/') {
        (prefix_path, String::new())
    } else {
        (
            prefix_path.parent().unwrap_or(Path::new("/")),
            prefix_path
                .file_name()
                .map(|s| s.to_string_lossy().into_owned())
                .unwrap_or_default(),
        )
    };
    let mut out = Vec::new();
    if let Ok(entries) = std::fs::read_dir(parent) {
        for entry in entries.flatten() {
            let name = entry.file_name();
            let name = name.to_string_lossy();
            if name.starts_with(&file_prefix) {
                let candidate = parent.join(&*name);
                let full = format!("{}{}", candidate.display(), suffix);
                let full_path = PathBuf::from(&full);
                if full_path.exists() {
                    out.push(full_path);
                }
            }
        }
    }
    out
}

fn main() {
    let tromp_dir = find_vendored_equi_miner_dir();
    let equi_miner_c = tromp_dir.join("equi_miner.c");
    assert!(
        equi_miner_c.exists(),
        "vendored equi_miner.c not found at {}",
        equi_miner_c.display()
    );

    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let harness_dir = manifest_dir.join("cross_check_c");
    let harness_main = harness_dir.join("harness_main.c");

    // Vendored portable BLAKE2b (repo-relative, no machine-specific path);
    // RZ_BLAKE2_REF_DIR overrides for building against a different copy.
    // Provenance: ../../../BLAKE/uniblake/PROVENANCE.md.
    let blake2_ref_dir = match env::var("RZ_BLAKE2_REF_DIR") {
        Ok(dir) => PathBuf::from(dir),
        Err(_) => manifest_dir.join("../../../BLAKE/vendor/blake2"),
    };
    let blake2b_ref_c = blake2_ref_dir.join("blake2b-ref.c");
    assert!(
        blake2b_ref_c.exists(),
        "vendored blake2b-ref.c not found at {} (see BLAKE/uniblake/PROVENANCE.md, \
         or set RZ_BLAKE2_REF_DIR)",
        blake2b_ref_c.display()
    );

    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());

    for ps in PARAM_SETS {
        let bin_name = format!("rz_xcheck_{}_{}", ps.wn, ps.restbits);

        // cc::Build always emits a static archive via try_compile(), not an
        // executable, so we use its compiler-discovery only and invoke the
        // resulting command ourselves to link a binary directly.
        let exe_path = out_dir.join(&bin_name);
        let mut cmd = cc::Build::new()
            .include(&tromp_dir)
            .include(&harness_dir)
            .include(&blake2_ref_dir)
            .define("WN", Some(ps.wn.to_string().as_str()))
            .define("WK", Some(ps.wk.to_string().as_str()))
            .define("RESTBITS", Some(ps.restbits.to_string().as_str()))
            .warnings(false)
            .opt_level(2)
            .get_compiler()
            .to_command();
        cmd.arg(&harness_main)
            .arg(&harness_dir.join("blake2b_glue.c"))
            .arg(&blake2b_ref_c)
            .arg("-o")
            .arg(&exe_path);
        let status = cmd.status().expect("failed to invoke C compiler");
        assert!(
            status.success(),
            "compiling cross-check binary {bin_name} failed"
        );

        println!(
            "cargo:rustc-env=RZ_XCHECK_BIN_{}_{}={}",
            ps.wn,
            ps.restbits,
            exe_path.display()
        );
    }

    println!("cargo:rerun-if-changed=cross_check_c/harness_main.c");
    println!("cargo:rerun-if-env-changed=RZ_EQUIHASH_TROMP_DIR");
}
