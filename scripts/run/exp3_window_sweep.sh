#!/bin/bash
# exp3 -- Fig. 13 (sliding window)

set -u
BIN=${BIN:-./build/runnable/main}
WL_WIKI=${WL_WIKI:-}      # wiki workload, 50% writes
OUT=${OUT:-./results}
REP=${REP:-1}
mkdir -p "$OUT"

for W in 1000 10000 100000 1000000 10000000; do   # five window sizes
  for R in $(seq 1 $REP); do
    # [Fig.13] MT x wiki50 x ESMT(Learning), window=$W, repetition $R
    $BIN --index_type 2 --ads_mod 9 --new_mode 3 --learing_score_option 32 --lr_feature_set 17 \
        --out_sched_tput 1 --score_window_size $W --process_cnt 3000000 --workload_path "$WL_WIKI" \
         --result_id w_i2_s${W}_r${R} --exp_log_name "$OUT/exp3_window.tsv"
  done
done