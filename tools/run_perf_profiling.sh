#!/usr/bin/env bash
set -euo pipefail

if [[ ${1:-} == "" ]]; then
  ITERS=30000000
else
  ITERS=$1
fi

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BENCH_DIR="$ROOT_DIR/build/benchmarks"
BINARY_NAMES=("mmx_neon_micro" "dynarec_micro" "dynarec_sanity")
BINARY_PATHS=(
  "$BENCH_DIR/mmx_neon_micro.app/Contents/MacOS/mmx_neon_micro"
  "$BENCH_DIR/dynarec_micro.app/Contents/MacOS/dynarec_micro"
  "$BENCH_DIR/dynarec_sanity.app/Contents/MacOS/dynarec_sanity"
)

MMX_ALLOW_BELOW=("PACKSSWB" "PACKUSWB" "3DNOW_PFRCP")
DYN_ALLOW_BELOW=("DYN_PSUBSB" "DYN_PSUBSW" "DYN_PSUBUSB" "DYN_PSUBUSW" "DYN_PMADDWD")

for i in "${!BINARY_NAMES[@]}"; do
  if [[ ! -x "${BINARY_PATHS[i]}" ]]; then
    echo "Missing benchmark binary: ${BINARY_PATHS[i]}" >&2
    exit 1
  fi
done

STAMP=$(date +%Y%m%d-%H%M%S)
LOG_ROOT="$ROOT_DIR/perf_logs/$STAMP"
mkdir -p "$LOG_ROOT"

echo "Writing logs to $LOG_ROOT"

run_and_log() {
  local label=$1
  shift
  local logfile="$LOG_ROOT/${label}.log"
  echo "\n=== Running $label ==="
  "$@" | tee "$logfile"
}

run_and_log mmx_neon "${BINARY_PATHS[0]}" --iters="$ITERS" --impl=neon
run_and_log dynarec_micro "${BINARY_PATHS[1]}" --iters="$ITERS" --impl=neon
run_and_log dynarec_sanity "${BINARY_PATHS[2]}"

MMX_ALLOW_ARGS=()
for name in "${MMX_ALLOW_BELOW[@]}"; do
  MMX_ALLOW_ARGS+=(--allow-below "$name")
done

DYN_ALLOW_ARGS=()
for name in "${DYN_ALLOW_BELOW[@]}"; do
  DYN_ALLOW_ARGS+=(--allow-below "$name")
done

python3 "$ROOT_DIR/tools/parse_mmx_neon_log.py" "$LOG_ROOT/mmx_neon.log" --output "$LOG_ROOT/mmx_neon.json" --min-ratio 0.5 "${MMX_ALLOW_ARGS[@]}"
python3 "$ROOT_DIR/tools/parse_mmx_neon_log.py" "$LOG_ROOT/dynarec_micro.log" --output "$LOG_ROOT/dynarec_micro.json" --min-ratio 0.5 "${DYN_ALLOW_ARGS[@]}" || true

echo "\nLogs: $LOG_ROOT"
ls "$LOG_ROOT"
