#!/bin/bash
# Install the dependencies of ESMT on Ubuntu 20.04 (host or docker image).
set -e

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    SUDO="sudo"
fi
export DEBIAN_FRONTEND=noninteractive

$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends \
    build-essential ca-certificates git wget python3 python3-pip \
    libssl-dev zlib1g-dev libbz2-dev liblz4-dev libsnappy-dev libzstd-dev libgflags-dev

# CMake >= 3.22
$SUDO pip3 install --no-cache-dir cmake==3.27.9

# RocksDB v8.11.3
git clone -q --depth 1 -b v8.11.3 https://github.com/facebook/rocksdb /tmp/rocksdb
cd /tmp/rocksdb
make shared_lib -j"$(nproc)"
$SUDO make install
$SUDO ldconfig
cd / && rm -rf /tmp/rocksdb

# boost 1.87.0 (program_options only)
wget -q https://archives.boost.io/release/1.87.0/source/boost_1_87_0.tar.gz -O /tmp/boost.tar.gz
mkdir -p /tmp/boost && tar -xzf /tmp/boost.tar.gz -C /tmp/boost --strip-components=1
cd /tmp/boost
./bootstrap.sh --with-libraries=program_options --prefix=/usr/local
$SUDO ./b2 install -j"$(nproc)"
$SUDO ldconfig
cd / && rm -rf /tmp/boost /tmp/boost.tar.gz

# LightGBM v4.3.0
git clone -q --recursive --depth 1 -b v4.3.0 https://github.com/microsoft/LightGBM /tmp/LightGBM
mkdir -p /tmp/LightGBM/build && cd /tmp/LightGBM/build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j"$(nproc)"
$SUDO make install
$SUDO ldconfig
cd / && rm -rf /tmp/LightGBM

echo "dependencies installed"
