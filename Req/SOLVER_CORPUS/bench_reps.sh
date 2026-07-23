#!/bin/bash
# 5-rep min/median/MAD timing harness, per Req/BENCH.md S2.
# Usage: bench_reps.sh <label> <command...>
set -e
label="$1"; shift
reps=5
times=()
for i in $(seq 1 $reps); do
  start=$(python3 -c "import time; print(time.perf_counter())")
  "$@" > /dev/null
  end=$(python3 -c "import time; print(time.perf_counter())")
  t=$(python3 -c "print(f'{$end - $start:.4f}')")
  times+=("$t")
done
python3 -c "
import statistics
times = [$(IFS=,; echo "${times[*]}")]
times_sorted = sorted(times)
med = statistics.median(times)
mad = statistics.median([abs(t - med) for t in times])
print(f'$label: min={times_sorted[0]:.4f}s median={med:.4f}s MAD={mad:.4f}s reps={times}')
"
