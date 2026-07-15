#!/usr/bin/env bash
# Template: copy into a new port directory, replace the TODO checks below
# with this port's actual prerequisites. See ../rz/check_env.sh for a
# filled-in example (vendored crate + reference BLAKE2b + C compiler).
set -u

ok=1

echo "== Rust toolchain =="
if command -v cargo >/dev/null 2>&1; then
    echo "  OK: $(cargo --version)"
else
    echo "  MISSING: no 'cargo' in PATH"
    ok=0
fi

# TODO: add one block per external prerequisite this port needs, e.g.:
#
# echo "== C compiler =="
# if command -v cc >/dev/null 2>&1; then
#     echo "  OK: $(command -v cc) ($(cc --version | head -1))"
# else
#     echo "  MISSING: no 'cc' in PATH"
#     ok=0
# fi
#
# echo "== <original reference source, e.g. a specific local clone> =="
# ref_path="$HOME/Work/ZK/ZKs/<clone-dir>"
# if [ -d "$ref_path" ]; then
#     echo "  OK: $ref_path"
# else
#     echo "  MISSING: $ref_path"
#     echo "    Fix: git clone <url> $ref_path"
#     ok=0
# fi

echo
if [ "$ok" -eq 1 ]; then
    echo "All prerequisites present."
    exit 0
else
    echo "One or more prerequisites missing (see MISSING lines above)."
    exit 1
fi
