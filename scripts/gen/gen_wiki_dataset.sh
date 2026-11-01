#!/bin/bash
# =============================================================================
# gen_wiki_dataset.sh -- Build the Wikipedia workloads used in the paper
#
#   Paper setting (Sec. 5):
#     * trace slice : lines 1,400,000,001 .. 1,415,000,000 of wiki2018.tr
#                     (the "second half" head), saved as $TMPTRACE
#     * operations  : 15,000,000 per workload
#     * write ratio : 5 / 25 / 50 / 75 / 95 (%)
#     * key remap   : ON (--remap_keys 1) -> keys become dense 0..n-1
#     * output      : wiki2018_0_<read>_<write>_0_15000000_0_<key_range>_<n>
#                     with key_range = 2^22 = 4194304 and n = 2742441
#
#   Input : raw Wikipedia trace (line format: <ts> <key> <object-size> <object-type>)
#           http://lrb.cs.princeton.edu/wiki2018.tr.tar.gz
#   Build : cmake .. && make wiki_workload_gen
#
#   Usage:
#     TRACE=/data/wiki2018.tr OUT=./workloads/wiki ./gen_wiki_dataset.sh
#     HALF=1 ...                 # use the first half of the trace instead
#     REMAP=0 ...                # keep raw keys (produces ..._<key_range>_<max_key>)
# =============================================================================
set -eu

TRACE=${TRACE:-./wiki2018.tr}          # raw trace
OUT=${OUT:-./workloads/wiki}           # output directory
TMPTRACE=${TMPTRACE:-/tmp/wiki2018_half2_head.tr}
HALF=${HALF:-2}                        # 1 = first half, 2 = second half (paper)
OPS=${OPS:-15000000}                   # operations per workload
REMAP=${REMAP:-1}                      # 1 = dense key remap (paper), 0 = raw keys
RATIOS=${RATIOS:-"5 25 50 75 95"}      # write ratios in percent
BIN=${BIN:-./build/runnable/wiki_workload_gen}
SLICE_START=${SLICE_START:-1400000001} # first line of the second half

if [ ! -x "$BIN" ]; then
  echo "[error] generator not found: $BIN (build it first: cmake .. && make wiki_workload_gen)" >&2
  exit 1
fi
if [ ! -f "$TRACE" ]; then
  echo "[error] raw trace not found: $TRACE" >&2
  echo "        download: http://lrb.cs.princeton.edu/wiki2018.tr.tar.gz" >&2
  exit 1
fi

mkdir -p "$OUT"

# ---- 1) slice the trace -----------------------------------------------------
if [ -f "$TMPTRACE" ]; then
  echo "[slice] reuse existing $TMPTRACE"
else
  if [ "$HALF" = "2" ]; then
    echo "[slice] lines $SLICE_START .. $((SLICE_START + OPS - 1)) -> $TMPTRACE"
    tail -n +"$SLICE_START" "$TRACE" | head -n "$OPS" > "$TMPTRACE"
  else
    echo "[slice] first $OPS lines -> $TMPTRACE"
    head -n "$OPS" "$TRACE" > "$TMPTRACE"
  fi
fi

# ---- 2) one workload per write ratio ---------------------------------------
for W in $RATIOS; do
  WR=$(awk "BEGIN{printf \"%.2f\", $W/100}")     # 0-1 double, two decimals
  echo "[gen] write_ratio=$WR ops=$OPS remap=$REMAP -> $OUT"
  # [paper] wiki workload generator, one file per write ratio
  "$BIN" --dataset "$TMPTRACE" --op_cnt "$OPS" --write_ratio "$WR" \
         --remap_keys "$REMAP" --out_dir "$OUT"
done

echo "[done] generated files:"
ls -1 "$OUT" | grep '^wiki2018_' || true
