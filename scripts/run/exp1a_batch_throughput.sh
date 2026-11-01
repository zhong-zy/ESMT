#!/bin/bash
# exp1a -- Fig. 7 / 9 / 10 (throughput)

set -u
BIN=${BIN:-./build/runnable/main}
WL_WIKI=${WL_WIKI:-}
OUT=${OUT:-./results}
REP=${REP:-1}
mkdir -p "$OUT"

for R in $(seq 1 $REP); do
  for IDX in 2 3 1 4; do
    for B in 100 1000 10000; do
      # [Fig.7/9/10] index $IDX x wiki50 x batch $B
      $BIN --index_type $IDX --ads_mod 2 --new_mode 0 --ads_update_batch $B \
           --process_cnt 3000000 --workload_path "$WL_WIKI" \
           --result_id w_i${IDX}_b${B}_r${R} --exp_log_name "$OUT/exp1a_batch_tput.tsv"
    done
    # [Fig.7/9/10] index $IDX x wiki50 x ESMT
    $BIN --index_type $IDX --ads_mod 9 --new_mode 3 --learing_score_option 32 --lr_feature_set 17 \
         --process_cnt 3000000 --workload_path "$WL_WIKI" \
         --result_id w_i${IDX}_esmt_r${R} --exp_log_name "$OUT/exp1a_batch_tput.tsv"
  done

  for PAIR in "10000:256" "40000:1024" "160000:4096" "640000:16384" "2560000:65536"; do
    SCALE=${PAIR%%:*}
    BUCKET=${PAIR##*:}
    for B in 100 1000 10000; do
      # [Fig.10] MBT x keys=$SCALE x bucket=$BUCKET x batch $B
      $BIN --index_type 3 --mbt_bucket_capacity $BUCKET --ads_mod 2 --new_mode 0 \
           --ads_update_batch $B --data_scale $SCALE --process_cnt 3000000 \
           --workload_path "$WL_WIKI" \
           --result_id k_i3_s${SCALE}_b${B}_r${R} --exp_log_name "$OUT/exp1a_batch_tput.tsv"
    done
    # [Fig.10] MBT x keys=$SCALE x bucket=$BUCKET x ESMT
    $BIN --index_type 3 --mbt_bucket_capacity $BUCKET --ads_mod 9 --new_mode 3 \
         --learing_score_option 32 --lr_feature_set 17 --data_scale $SCALE --process_cnt 3000000 \
         --workload_path "$WL_WIKI" \
         --result_id k_i3_s${SCALE}_esmt_r${R} --exp_log_name "$OUT/exp1a_batch_tput.tsv"
  done
done
