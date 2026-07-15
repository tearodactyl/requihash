#!/usr/bin/env bash
# Checks the same prerequisites build.rs checks, before running cargo.
# See ENVIRONMENT.md for what each one is and why it's needed.
set -u

ok=1

echo "== C compiler =="
if command -v cc >/dev/null 2>&1; then
    echo "  OK: $(command -v cc) ($(cc --version | head -1))"
else
    echo "  MISSING: no 'cc' in PATH"
    ok=0
fi

echo "== Vendored equihash-0.3.0/tromp/ (registry cache, or RZ_EQUIHASH_TROMP_DIR) =="
if [ -n "${RZ_EQUIHASH_TROMP_DIR:-}" ]; then
    tromp_dir="$RZ_EQUIHASH_TROMP_DIR"
    src="RZ_EQUIHASH_TROMP_DIR override"
else
    tromp_dir=$(ls -d "$HOME"/.cargo/registry/src/index.crates.io-*/equihash-0.3.0/tromp 2>/dev/null | head -1)
    src="cargo registry cache"
fi
if [ -n "$tromp_dir" ] && [ -f "$tromp_dir/equi_miner.c" ]; then
    echo "  OK: $tromp_dir/equi_miner.c ($src)"
else
    echo "  MISSING: no equi_miner.c found via $src"
    echo "    Fix: build anything that depends on equihash = \"0.3\" once to populate"
    echo "    the registry cache, or set RZ_EQUIHASH_TROMP_DIR to a local checkout."
    ok=0
fi

echo "== Reference BLAKE2b (hardcoded path, see ENVIRONMENT.md) =="
blake2_ref="$HOME/Work/ZK/ZKs/blake2-reference/ref/blake2b-ref.c"
if [ -f "$blake2_ref" ]; then
    echo "  OK: $blake2_ref"
else
    echo "  MISSING: $blake2_ref"
    echo "    Fix: git clone https://github.com/BLAKE2/BLAKE2 ~/Work/ZK/ZKs/blake2-reference"
    ok=0
fi

echo "== Rust toolchain =="
if command -v cargo >/dev/null 2>&1; then
    echo "  OK: $(cargo --version)"
else
    echo "  MISSING: no 'cargo' in PATH"
    ok=0
fi

echo
if [ "$ok" -eq 1 ]; then
    echo "All prerequisites present. 'cargo test' should be able to build."
    exit 0
else
    echo "One or more prerequisites missing (see MISSING lines above)."
    exit 1
fi
