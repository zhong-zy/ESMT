#!/bin/bash
# exp6 -- hit-rate table (feature-set ablation)

set -u
BIN=${BIN:-./build/runnable/main}
WL_ETH=${WL_ETH:-}        # ETH workload, 50% writes
OUT=${OUT:-./results}
REP=${REP:-1}
mkdir -p "$OUT"

for IDX in 2 4; do              # 2=MT 4=LETUS
  for FS in 12 14 17; do
    for R in $(seq 1 $REP); do
      # [Table] index $IDX x ETH x ESMT(Learning) x FS$FS, repetition $R
      $BIN --index_type $IDX --ads_mod 9 --new_mode 3 --learing_score_option 32 \
           --lr_feature_set $FS --process_cnt 3000000 --workload_path "$WL_ETH" \
           --result_id i${IDX}_fs${FS}_r${R} --exp_log_name "$OUT/exp6_fs_ablation.tsv"
    done
  done
done