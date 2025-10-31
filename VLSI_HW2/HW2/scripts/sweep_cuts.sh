#!/usr/bin/env bash
set -euo pipefail

# This script searches for the best seed (by total cut size) across four runs:
#   1) ../bin/hw2 ../testcase/public2.txt ../output/p2-4 4
#   2) ../bin/hw2 ../testcase/public2.txt ../output/p2-4 2
#   3) ../bin/hw2 ../testcase/public1.txt ../output/p2-4 4
#   4) ../bin/hw2 ../testcase/public1.txt ../output/p2-4 2
# It sums the cut sizes and reports the minimum-total seed.
#
# Usage:
#   SEED_START=1 SEED_END=50 ./sweep_cuts.sh
#   # or override commands (rarely needed): CMDS=("cmd1" "cmd2" ...) ./sweep_cuts.sh
#
# Notes:
# - The FM binary reads the seed from environment variable FM_SEED.
# - We parse the last line containing "cut size" (case-insensitive) from stdout.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"  # points to HW2
SRC_DIR="$ROOT_DIR/src"

# Default seed range (override via env)
SEED_START="${SEED_START:-1}"
SEED_END="${SEED_END:-30}"

# Commands to run (relative to src dir)
if [[ -z "${CMDS+x}" ]]; then
  # Declare as bash array if not provided by caller
  declare -a CMDS=(
    "../bin/hw2 ../testcase/public1.txt ../output/p1-2 2"
    "../bin/hw2 ../testcase/public1.txt ../output/p1-4 4"
    "../bin/hw2 ../testcase/public2.txt ../output/p2-2 2"
    "../bin/hw2 ../testcase/public2.txt ../output/p2-4 4"
  )
fi

# Ensure output directories exist (not strictly required, but helpful)
mkdir -p "$ROOT_DIR/output/p1-2" "$ROOT_DIR/output/p1-4" "$ROOT_DIR/output/p2-2" "$ROOT_DIR/output/p2-4" || true

pushd "$SRC_DIR" >/dev/null

best_sum=""
best_seed=""

run_and_parse() {
  local seed="$1"; shift
  local cmd_line="$*"
  # Split into tokens: bin input output way
  local -a parts
  # shellcheck disable=SC2206
  parts=($cmd_line)
  if (( ${#parts[@]} < 4 )); then
    echo "ERROR: malformed command: $cmd_line" >&2
    return 2
  fi
  local bin="${parts[0]}" in="${parts[1]}" out="${parts[2]}" way="${parts[3]}"
  # If the output path is a directory, write to a deterministic file under it
  if [[ -d "$out" ]]; then
    local base
    base="seed_${seed}_$(basename "$in")_w${way}.out"
    out="${out%/}/$base"
  fi
  mkdir -p "$(dirname "$out")"
  local effective_cmd="$bin $in $out $way"
  # Log the effective command to stderr so stdout remains only the parsed cut value
  echo "+ RUN: $effective_cmd" >&2
  # Run with FM_SEED set, capture all output
  local out
  if ! out=$(FM_SEED="$seed" bash -lc "$effective_cmd" 2>&1); then
    echo "ERROR: command failed with seed=$seed" >&2
    echo "$out" >&2
    return 2
  fi
  # Parse the last occurrence of a line mentioning cut size, get the last integer on that line
  local cut
  cut=$(echo "$out" | grep -i "cut size" | tail -n 1 | grep -Eo "[0-9]+" | tail -n 1 || true)
  if [[ -z "$cut" ]]; then
    # Fallback: try also lines like "INITIAL (net-based) CUT SIZE: X" (still matched above), else dump log
    mkdir -p ./tmp
    local log="./tmp/hw2_seed_${seed}_$(date +%s).log"
    echo "$out" > "$log"
    echo "ERROR: failed to parse cut size for seed=$seed; saved log: $log" >&2
    return 3
  fi
  echo "$cut"
}

for ((seed=SEED_START; seed<=SEED_END; seed++)); do
  echo "=== Seed $seed ==="
  total=0
  for cmd in "${CMDS[@]}"; do
    cut_val=$(run_and_parse "$seed" "$cmd") || exit $?
    echo "  -> cut size = $cut_val"
    total=$(( total + cut_val ))
  done
  echo "Seed $seed: total cut size = $total"
  if [[ -z "$best_sum" || $total -lt $best_sum ]]; then
    best_sum=$total
    best_seed=$seed
  fi
  echo
done

echo "=== BEST ==="
echo "Best seed: $best_seed"
echo "Best total cut size: $best_sum"

popd >/dev/null
