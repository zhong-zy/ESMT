# QLDB

The experiments are based on the VeriBench ledger implementation
(`https://github.com/nusdbsystem/LedgerDatabase`, commit `278880c`). The complete tree is
included here, so the upstream repository does not have to be cloned.

## Build

Dependencies: protobuf, boost (filesystem, program_options, thread), cryptopp, tbb,
rocksdb, zstd / snappy / lz4 / bz2 / z, OpenSSL.

```shell
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DLEDGERDB=OFF -DAMZQLDB=ON
make -j
```

## Run

The workload is the ETH trace described in `../docs/datasets.md`. Every transaction is a
two-read / two-write group:

```shell
LD_LIBRARY_PATH=$PWD/lib ./bin/bench_qldb_eth \
    --workload <eth_workload> --db /tmp/qldb_db/run \
    --txn_mode 2r2w --warmup_rows 4000000 --measure_rows 500000 --sample 100 \
    --out /tmp/qldb_prof/run.tsv
```

`--warmup_rows` counts trace rows, `--measure_rows` counts measured transactions, and
`--sample` records one out of every *k* transactions (500,000 measured transactions give
5,000 rows).

## Output

One row per transaction, one column per stage (all in nanoseconds):

| Column | Stage |
| --- | --- |
| `w_data_ns` | data update: version look-up, encoding, B+ tree insertion, block append |
| `w_mt_block_ns` | block hash (`calculateBlockHash`) |
| `w_mt_digest_ns` | block-digest tree update (`calculateDigest`) |
| `w_lookup_ns` | post-write read-back |
| `w_proof_ns` | proof generation and verification |
| `r_lookup_ns`, `r_proof_ns` | read path: look-up and proof |
| `total_ns` | whole transaction |

Merkle path maintenance is `w_mt_block_ns + w_mt_digest_ns`, the data update is
`w_data_ns`, and the write path is the sum of the two plus `w_proof_ns` (the read-back
`w_lookup_ns` and `w_other_ns` are excluded). The shares reported in the paper are those
three components divided by the write path:

```
merkle = w_mt_block_ns + w_mt_digest_ns
data   = w_data_ns
wpath  = merkle + data + w_proof_ns
share  = merkle / wpath,  data / wpath
```
