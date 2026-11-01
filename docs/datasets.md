# Datasets

## 1. Wikipedia (wiki2018)

Download the trace and extract it:

```shell
wget http://lrb.cs.princeton.edu/wiki2018.tr.tar.gz
tar -xzvf wiki2018.tr.tar.gz        # -> wiki2018.tr
```

Take the second half of the trace (15M records) and generate the workloads:

```shell
tail -n +1400000001 wiki2018.tr | head -n 15000000 > /tmp/wiki2018_half2_head.tr

./build/runnable/wiki_workload_gen \
    --dataset     /tmp/wiki2018_half2_head.tr \
    --op_cnt      15000000 \
    --write_ratio 0.50 \
    --remap_keys  1 \
    --out_dir     ./workloads/wiki
```

Run it once per write ratio used in the paper (0.05 / 0.25 / 0.50 / 0.75 / 0.95), or use
`./scripts/gen/gen_wiki_dataset.sh`. Each output file is named

```
wiki2018_0_<read>_<write>_0_<op_cnt>_0_<key_range>_<keys>
```

## 2. ETH (Ethereum transactions)

Export the transactions from Google BigQuery
([public dataset](https://cloud.google.com/blog/products/data-analytics/introducing-six-new-cryptocurrencies-in-bigquery-public-datasets-and-how-to-analyze-them)),
blocks 9,000,000 – 9,030,000:

```SQL
-- EXPORT DATA OPTIONS(
--   uri='gs://<bucket>/eth.csv',
--   format='CSV',
--   overwrite=true,
--   header=true,
--   field_delimiter=',') AS
SELECT
  block_number, transaction_index, from_address, to_address, value
FROM
  bigquery-public-data.goog_blockchain_ethereum_mainnet_us.transactions
WHERE
  block_number >= 9000000 and block_number < 9030000
ORDER BY block_number, transaction_index
;
```

Convert the exported CSV into the workload format:

```shell
python3 scripts/gen/gen_eth_dataset.py --input eth.csv \
                                       --out_dir ./workloads/eth \
                                       --block_start 9000000 --block_end 9030000
```

Each transaction becomes four operations — `(from, read) (from, write) (to, read) (to, write)` —
so the workload has 13,516,332 operations over 783,313 distinct addresses
(`eth_0_0_0_0_13516332_0_1048576_783313`). The write value is the transferred amount.
