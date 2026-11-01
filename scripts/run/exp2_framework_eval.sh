#!/bin/bash
# exp2 -- Fig. 12 (framework evaluation)

set -u
BIN=${BIN:-./build/runnable/main}
WL_WIKI=${WL_WIKI:-}      # wiki workload, 50% writes
WL_ETH=${WL_ETH:-}        # ETH workload, 50% writes
OUT=${OUT:-./results}
REP=${REP:-1}
mkdir -p "$OUT"

for IDX in 2 3 1 4; do          # 2=MT 3=MBT 1=MPT 4=LETUS
  for DS in wiki eth; do
    if [ "$DS" = "wiki" ]; then WL="$WL_WIKI"; else WL="$WL_ETH"; fi
    for M in seq lazy eseq efreq elearn; do
      case $M in
        seq)    FLAGS="--ads_mod 0 --new_mode 0" ;;
        lazy)   FLAGS="--ads_mod 1 --new_mode 0" ;;
        eseq)   FLAGS="--ads_mod 5 --new_mode 0" ;;
        efreq)  FLAGS="--ads_mod 9 --new_mode 1" ;;
        elearn) FLAGS="--ads_mod 9 --new_mode 3 --learing_score_option 32 --lr_feature_set 17" ;;
      esac
      for R in $(seq 1 $REP); do
        # [Fig.12] index $IDX x $DS x method=$M, repetition $R
        $BIN $FLAGS --index_type $IDX --process_cnt 3000000 --workload_path "$WL" \
             --result_id ${DS}_i${IDX}_${M}_r${R} --exp_log_name "$OUT/exp2_framework.tsv"
      done
    done
  done
done