#!/bin/bash
# exp1b -- Fig. 8 / 11 (latency)

set -u
BIN=${BIN:-./build/runnable/main}
WL_WIKI=${WL_WIKI:-}
WL_ETH=${WL_ETH:-}
OUT=${OUT:-./results}
RATE=${RATE:-100000}
REP=${REP:-1}
mkdir -p "$OUT"

for R in $(seq 1 $REP); do
  for IDX in 2 3 1 4; do
    for B in 100 1000 10000; do
      # [Fig.8] index $IDX x wiki50 x batch $B x send_ops $RATE
      $BIN --index_type $IDX --ads_mod 2 --new_mode 0 --ads_update_batch $B \
           --process_cnt 1000000 --out_latency 1 --send_ops $RATE --workload_path "$WL_WIKI" \
           --result_id wl_i${IDX}_b${B}_r${R} --exp_log_name "$OUT/exp1b_batch_lat.tsv"
    done
    # [Fig.8] index $IDX x wiki50 x ESMT x send_ops $RATE
    $BIN --index_type $IDX --ads_mod 9 --new_mode 3 --learing_score_option 32 --lr_feature_set 17 \
         --process_cnt 1000000 --out_latency 1 --send_ops $RATE --workload_path "$WL_WIKI" \
         --result_id wl_i${IDX}_esmt_r${R} --exp_log_name "$OUT/exp1b_batch_lat.tsv"

    for B in 2 4 8 16; do
      # [Fig.11] index $IDX x eth x batch $B x send_ops $RATE
      $BIN --index_type $IDX --ads_mod 2 --new_mode 0 --ads_update_batch $B \
           --process_cnt 1000000 --out_latency 1 --send_ops $RATE --workload_path "$WL_ETH" \
           --result_id el_i${IDX}_b${B}_r${R} --exp_log_name "$OUT/exp1b_batch_lat.tsv"
    done
    # [Fig.11] index $IDX x eth x ESMT x send_ops $RATE
    $BIN --index_type $IDX --ads_mod 9 --new_mode 3 --learing_score_option 32 --lr_feature_set 17 \
         --process_cnt 1000000 --out_latency 1 --send_ops $RATE --workload_path "$WL_ETH" \
         --result_id el_i${IDX}_esmt_r${R} --exp_log_name "$OUT/exp1b_batch_lat.tsv"
  done
done
