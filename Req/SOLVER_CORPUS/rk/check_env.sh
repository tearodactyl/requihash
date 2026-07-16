#!/usr/bin/env bash
# RK has no build-time external dependency: unlike RZ (which compiles a
# vendored C solver via build.rs), RK's KAT vectors were generated once
# from a scratch build of the C++ original (README.md "Validation") and
# are committed as static JSON in vectors/ -- cargo test/build needs only
# the Rust toolchain.
set -u

ok=1

echo "== Rust toolchain =="
if command -v cargo >/dev/null 2>&1; then
    echo "  OK: $(cargo --version)"
else
    echo "  MISSING: no 'cargo' in PATH"
    ok=0
fi

echo "== Committed KAT vectors =="
vec_dir="$(dirname "$0")/vectors"
count=$(find "$vec_dir" -name '*.json' 2>/dev/null | wc -l | tr -d ' ')
if [ "$count" -ge 8 ]; then
    echo "  OK: $count vector file(s) in $vec_dir"
else
    echo "  MISSING: expected at least 8 vector files in $vec_dir, found $count"
    ok=0
fi

echo
echo "(Regenerating vectors from the C++ original is not part of normal"
echo " build/test -- only needed if extending vectors/ to new (n,k)"
echo " points; see README.md 'Build environment' for how the original"
echo " was made to build on arm64.)"

echo
if [ "$ok" -eq 1 ]; then
    echo "All prerequisites present."
    exit 0
else
    echo "One or more prerequisites missing (see MISSING lines above)."
    exit 1
fi
