#!/bin/sh
# Standard req_bench run for the reference machine series (BENCH.md
# discipline): compare against the rolling apple-silicon-dev baseline, then
# append this run's records to it. Extra args pass through (e.g. --family,
# --band-pct 3). Run from anywhere; works from this script's directory.
#
#   ./bench.sh                  # standard run
#   ./bench.sh --family         # include the heavy SPEC family campaign
#   REQ_BENCH_TAG=<tag> ./bench.sh --json ../baselines/<tag>.jsonl   # other machine
cd "$(dirname "$0")" || exit 1
exec cargo run --release --bin req_bench -- \
  --json ../baselines/apple-silicon-dev.jsonl \
  --tag apple-silicon-dev \
  "$@"
