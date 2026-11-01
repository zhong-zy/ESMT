#!/bin/bash
# exp5 -- Fig. 15 / 16 (breakdown, root size)

set -u
BIN=${BIN:-./build/runnable/main}
WL_DIR=${WL_DIR:-}
OUT=${OUT:-./results}
REP=${REP:-1}
mkdir -p "$OUT"

for R in $(seq 1 $REP); do
  for PAIR in "05:0.95" "25:0.75" "50:0.50" "75:0.25" "95:0.05"; do
    W=${PAIR%%:*}
    RD=${PAIR##*:}
    WL="$WL_DIR/wiki2018_0_${RD}_0.${W}_0_15000000_0_4194304_2742441"
    # [Fig.15/16] MT x wiki50(write ${W}%) x Sequential with inlined maintenance
    SEQ_INLINE=1 $BIN --index_type 2 --ads_mod 0 --new_mode 0 --process_cnt 3000000 --out_breakdown 1 \
        --workload_path "$WL" \
        --result_id seq_wr${W}_r${R} --exp_log_name "$OUT/exp5_breakdown.tsv"
    # [Fig.15/16] MT x wiki50(write ${W}%) x ESMT(Learning)
    $BIN --index_type 2 --ads_mod 9 --new_mode 3 --learing_score_option 32 --lr_feature_set 17 \
        --process_cnt 3000000 --out_breakdown 1 --workload_path "$WL" \
        --result_id esmt_wr${W}_r${R} --exp_log_name "$OUT/exp5_breakdown.tsv"
  done
done
