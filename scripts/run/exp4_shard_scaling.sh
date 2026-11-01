#!/bin/bash
# exp4 -- Fig. 14 (weak scaling)

set -u
BIN=${BIN:-./build/runnable/main}
WL_WIKI=${WL_WIKI:-}      # wiki workload, 50% writes
OUT=${OUT:-./results}
REP=${REP:-1}
SB=${SB:-128}             # score_batch for the ESMT arm
mkdir -p "$OUT"

for S in 1 2 4 8; do
  SCALE=$((320000 * S))       # keys grow linearly with shards (weak scaling)
  PROC=$((375000 * S))        # measured ops per shard kept constant
  BATCH=$((10000 * S))        # batch arm: 10^4 per shard

  for R in $(seq 1 $REP); do
    # [Fig.14] MT x wiki50 x ESMT(Learning) x shard=$S (data_scale=$SCALE, process_cnt=$PROC)
    $BIN --index_type 2 --shard_cnt $S --shard_mode 0 --data_shard 1 \
         --data_scale $SCALE --process_cnt $PROC --score_batch $SB \
         --ads_mod 9 --new_mode 3 --learing_score_option 32 --lr_feature_set 17 \
         --workload_path "$WL_WIKI" \
         --result_id A_s${S}_r${R} --exp_log_name "$OUT/exp4_shard.tsv"

    # [Fig.14] MT x wiki50 x per-block batch baseline x shard=$S (batch = 10^4 per shard)
    $BIN --index_type 2 --shard_cnt $S --shard_mode 0 --data_shard 1 \
         --data_scale $SCALE --process_cnt $PROC --ads_update_batch $BATCH \
         --ads_mod 2 --new_mode 0 \
         --workload_path "$WL_WIKI" \
         --result_id B_s${S}_r${R} --exp_log_name "$OUT/exp4_shard.tsv"
  done
done