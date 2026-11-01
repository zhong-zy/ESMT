#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <tuple>
#include <cstdlib>
#include "cpputils/random.h"
#include <boost/program_options.hpp>

namespace po = boost::program_options;

using namespace std;

int main(int argc, char *argv[]) {
    po::options_description desc("Allowed options");
    desc.add_options()
    ("dataset", po::value<string>()->default_value("/tmp/wiki2018_half2_head.tr"), "")
    ("op_cnt", po::value<int>(), "")
    ("write_ratio", po::value<double>(), "0-1 (float)")
    ("skip_lines", po::value<uint64_t>()->default_value(0), "skip the first N lines of the dataset")
    ("remap_keys", po::value<int>()->default_value(0), "1 = ignore keys not appearing in this file, remap appeared keys to 0..n-1 (ascending original key order)")
    ("out_dir", po::value<string>()->default_value("workloads/wiki"), "output directory")
    ;
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    auto dataset = vm["dataset"].as<string>();
    auto max_ops = vm["op_cnt"].as<int>();
    auto write_ratio = vm["write_ratio"].as<double>();
    auto skip_lines = vm["skip_lines"].as<uint64_t>();
    auto remap_keys = (vm["remap_keys"].as<int>() != 0);
    auto out_dir = vm["out_dir"].as<string>();
    system(("mkdir -p " + out_dir).c_str());

    bool gen_for_mt = true;
    double read_ratio = 1-write_ratio;
    ifstream in(dataset, ios::in);
    uint64_t ts, key, size, type;
    uint64_t max_key = 0;
    vector<tuple<uint64_t, uint64_t , uint64_t, uint64_t>> data;
    uint64_t cnt = 0;
    uint64_t skipped = 0;
    while (in >> ts >> key >> size >> type) {
        if (skipped < skip_lines) {
            ++skipped;
            continue;
        }
        max_key = max(key, max_key);
        data.push_back({ts, key, size, type});
        ++cnt;
        if (cnt >= max_ops) {
            break;
        }
    }

    uint64_t key_cnt = max_key;
    if (remap_keys) {
        vector<uint8_t> seen(max_key + 1, 0);
        for (const auto &item: data) {
            seen[std::get<1>(item)] = 1;
        }
        vector<uint32_t> key_map(max_key + 1, 0);
        uint64_t n = 0;
        ofstream mout(out_dir + "/wiki2018_key_remap.txt");
        for (uint64_t k = 0; k <= max_key; ++k) {
            if (seen[k]) {
                key_map[k] = static_cast<uint32_t>(n);
                mout << k << "\t" << n << "\n";
                ++n;
            }
        }
        mout.close();
        for (auto &item: data) {
            std::get<1>(item) = key_map[std::get<1>(item)];
        }
        key_cnt = n;
        max_key = n - 1;
        cout << "distinct keys (n): " << n << endl;
        cout << "key remap file: " << out_dir << "/wiki2018_key_remap.txt" << endl;
    }

    uint64_t key_range = 1;
    if (gen_for_mt) {
        do {
            key_range <<= 1;
        } while (key_range < max_key);
    } else {
        key_range = max_key;
    }

    stringstream name;
    name << std::fixed << std::setprecision(2) << out_dir << "/wiki2018_0_"
    << read_ratio << "_" << (1-read_ratio) << "_0_" << max_ops << "_0_"
    << key_range << "_" << (remap_keys ? key_cnt : max_key);
    ofstream out(name.str());
    cpputils::RandReal<double> rand_double(0, 1);
    for (const auto &[ts, key, size, type]: data) {
        auto r = rand_double();
        if (r <= read_ratio) {
            out << key << "\t" << 1 << "\t" << 0 << "\t" << 0 << "\t" << "" << endl;
        } else {
            out << key << "\t" << 2 << "\t" << 0 << "\t" << 0 << "\t" << to_string(size)+"-"+ to_string(type) << endl;
        }
    }
}
