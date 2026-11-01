# ESMT

## Dependencies
- C++ >= 17
- Cmake >= 3.22
- RocksDB: v8.11.3
- LightGBM: v4.3.0
- boost: 1.87.0

## Project Structure

```
ESMT/
├── CMakeLists.txt      top-level build
├── include/            headers
├── runnable/           experiment driver (main.cpp)
├── scripts/
│   ├── run/            exp1a ... exp6, one script per figure/table
│   └── gen/            workload generation (wiki / ETH)
├── docs/datasets.md    how the two datasets are produced
├── qldb/               QLDB implementation
└── env/Dockerfile      build environment
```

## Installation

### 1. Environment preparation

We recommend building and running ESMT in an Ubuntu 20.04 environment (in docker or on a host
machine).

For docker:
```shell
cd env
docker build -t esmt -f Dockerfile ..
docker run -it --rm esmt
```

For host machine:
```shell
cd env
sudo ./install_deps.sh
```

### 2. Building ESMT

```shell
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j
```

Binaries: `build/runnable/main` (experiment driver) and `build/runnable/wiki_workload_gen`.

## Dataset

Two real-world datasets are used to generate the workloads.

### Wiki Trace

The Wiki trace dataset is a trace collected on a west-coast CDN node, which serves photos and
other media content for Wikipedia pages. The trace contains a sequence of requests with Wikidata
IDs. Leveraging this dataset, we construct workloads across two parametric dimensions, i.e., the
number of keys and the read/write ratio, to evaluate the performance of Merkle path update
approaches. The workloads contain only read and write operations, and are referenced by their
write ratio (where read ratio = 1 − write ratio).

### ETH Transactions

The ETH transaction dataset records the blocks and transactions of the Ethereum blockchain. We
generate a workload based on real-world Ethereum transaction data from block 9,000,000 to
9,030,000. Each transaction is treated as an inter-account transfer and is translated into two
read operations and two write operations, so the generated workload contains 50% reads and 50%
writes. The number of keys that appear in these transactions is 783,313. This workload
supplements the evaluation with the canonical state-transfer scenario of verifiable ledger
databases.

See `docs/datasets.md` for how both files are generated.

## Experiments

`scripts/run/exp*.sh` — one script per figure or table. The workload path is passed through an
environment variable:

```shell
WL_WIKI=./workloads/wiki/wiki2018_0_0.50_0.50_0_15000000_0_4194304_2742441 \
./scripts/run/exp3_window_sweep.sh
```

| Script | Figure |
| --- | --- |
| `exp1a_batch_throughput.sh` | Fig. 7 / 9 / 10 |
| `exp1b_batch_latency.sh` | Fig. 8 / 11 |
| `exp2_framework_eval.sh` | Fig. 12 |
| `exp3_window_sweep.sh` | Fig. 13 |
| `exp4_shard_scaling.sh` | Fig. 14 |
| `exp5_breakdown_rootsize.sh` | Fig. 15 / 16 |
| `exp6_fs_ablation.sh` | hit-rate table |

`BIN` sets the driver path (default `./build/runnable/main`), `OUT` the result directory
(default `./results`).
