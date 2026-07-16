// Compiles the repository's single vendored BLAKE2b (../blake2, see its
// PROVENANCE.md) plus this crate's own small C accessor file. Repo-relative
// only; BLAKE2REF_DIR overrides.
use std::env;
use std::path::PathBuf;

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let vendor = match env::var("BLAKE2REF_DIR") {
        Ok(d) => PathBuf::from(d),
        Err(_) => manifest.join("../blake2"),
    };
    let ref_c = vendor.join("blake2b-ref.c");
    assert!(
        ref_c.exists(),
        "vendored blake2b-ref.c not found at {} (see BLAKE/vendor/blake2/PROVENANCE.md)",
        ref_c.display()
    );
    cc::Build::new()
        .file(&ref_c)
        .file(manifest.join("csrc/blake2ref_glue.c"))
        .include(&vendor)
        .opt_level(2)
        .compile("blake2ref");
    println!("cargo:rerun-if-changed=csrc/blake2ref_glue.c");
    println!("cargo:rerun-if-changed={}", ref_c.display());
    println!("cargo:rerun-if-env-changed=BLAKE2REF_DIR");
}
