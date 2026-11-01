#!/usr/bin/env python3
"""Build the ETH workload from the BigQuery CSV export.

Input : eth.csv exported by BigQuery, columns
        block_number,transaction_index,from_address,to_address,value
Output: ./workloads/eth/eth_0_0_0_0_<ops>_0_<key_range>_<keys>

Every transaction contributes four operations:
    (from, read) (from, write) (to, read) (to, write)
Addresses are mapped to dense ids 0..n-1 in sorted (hex string) order, and the
key space is rounded up to a power of two, so that
    ops       = 4 * number of transactions
    key_range = the smallest power of two >= number of distinct addresses.

Each output line is:  <key> \t <op> \t 0 \t 0 \t <value>   (op = 1 read, 2 write)

Usage:
    python3 gen_eth_dataset.py --input eth.csv --out_dir ./workloads/eth
                               [--block_start 9000000] [--block_end 9030000]
"""
import argparse
import os

READ, WRITE = 1, 2
HEADER_PREFIX = "block_number,transaction_index"


def parse_args():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", default="eth.csv", help="BigQuery CSV export")
    ap.add_argument("--out_dir", default="./workloads/eth", help="output directory")
    ap.add_argument("--block_start", type=int, default=9000000, help="first block (inclusive)")
    ap.add_argument("--block_end", type=int, default=9030000, help="last block (exclusive)")
    return ap.parse_args()


def records(path, block_start, block_end):
    """Yield (from_key, to_key, value) for the transactions inside the block range."""
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith(HEADER_PREFIX):
                continue
            bid, _txid, key_from, key_to, value = line.split(",")
            bid = int(bid)
            if bid < block_start:
                continue
            if bid >= block_end:
                break
            yield key_from, key_to, value


def main():
    args = parse_args()

    # pass 1: collect the distinct addresses
    keys = set()
    for key_from, key_to, _ in records(args.input, args.block_start, args.block_end):
        keys.add(key_from)
        keys.add(key_to)

    if not keys:
        raise SystemExit(f"no transaction found in [{args.block_start}, {args.block_end})")

    nkeys = {k: i for i, k in enumerate(sorted(keys))}
    key_range = 1
    while key_range < len(nkeys):
        key_range <<= 1

    # pass 2: write the workload (ops are counted while writing, hence the temp name)
    os.makedirs(args.out_dir, exist_ok=True)
    tmp_name = os.path.join(args.out_dir, "eth.tmp")
    ops = 0
    with open(tmp_name, "w", encoding="utf-8") as wf:
        for key_from, key_to, value in records(args.input, args.block_start, args.block_end):
            k1, k2 = nkeys[key_from], nkeys[key_to]
            print(k1, READ, 0, 0, "", sep="\t", file=wf)
            print(k1, WRITE, 0, 0, value, sep="\t", file=wf)
            print(k2, READ, 0, 0, "", sep="\t", file=wf)
            print(k2, WRITE, 0, 0, value, sep="\t", file=wf)
            ops += 4

    final_name = os.path.join(
        args.out_dir, f"eth_0_0_0_0_{ops}_0_{key_range}_{len(nkeys)}")
    os.replace(tmp_name, final_name)

    print(f"blocks    : [{args.block_start}, {args.block_end})")
    print(f"txs       : {ops // 4}")
    print(f"ops       : {ops}")
    print(f"addresses : {len(nkeys)}  (key range {key_range})")
    print(f"output    : {final_name}")


if __name__ == "__main__":
    main()
