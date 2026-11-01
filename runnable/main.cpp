
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>

#include "workloadsim/processor.h"
#include "workloadsim/data_init.h"
#include "workloadsim/workload_simulator.h"
#include "cpputils/string_utils.h"

#include "config.h"
#include <boost/program_options.hpp>

using namespace std;
namespace fs = std::filesystem;
namespace po = boost::program_options;
#define VERBOSE

int main(int argc, char *argv[]) {

    po::options_description desc("Allowed options");
    desc.add_options()
            ("ads_mod", po::value<int>(), "0: Sequential, 1: Lazy, 5: Sequential+, 9: New Base")

            ("new_mode", po::value<int>(), "probability=1, future_distance, history_distance")
            ("index_type", po::value<int>(), "MerkleTree=2, MPT=1, MBT=3, LETUS=4")
            ("tree_cnt", po::value<int>()->default_value(0), "for RANDOM")
            ("shard_cnt", po::value<int>()->default_value(1), "number of shards (each shard = one independent Merkle tree + maintenance thread); 1=off")
            ("shard_mode", po::value<int>()->default_value(0), "0=hash(key)%%S, 1=equal key range")
            ("mbt_bucket_capacity", po::value<int>()->default_value(65536), "MBT fixed bucket count (IndexBucketTree; default 65536 = legacy)")
            ("score_buf", po::value<int>()->default_value(1), "handshake buffer between submission and scoring; <=0 means unbounded (total ops)")
            ("out_latency", po::value<int>()->default_value(0), "1 = also report the latency columns (avg / p90 / p99)")
            ("out_breakdown", po::value<int>()->default_value(0), "1 = also report the phase breakdown (request / scheduling / Merkle path update)")
            ("out_sched_tput", po::value<int>()->default_value(0), "1 = also report the schedule generation throughput")
            ("ads_store_type", po::value<int>()->default_value(2), "ROCKSDB=0, DISK, Memory")
            ("score_window_size", po::value<int>()->default_value(1000000),"")
            ("ads_coalesce", po::value<int>()->default_value(0), "ads queue coalescing: 0=off, 1=one entry per dirty key")
            ("textbook_preread_size", po::value<int>()->default_value(0), "")
            ("max_delta_cnt", po::value<int>()->default_value(3), "")
            ("freq_base_file", po::value<std::string>()->default_value(""), "offline key frequency base file (new_mode=frequency, score_window_size<=0)")
            ("freq_ewma_halflife", po::value<int>()->default_value(0), "EWMA freq half-life in ops (new_mode=frequency; 0=off=hard window)")
            ("placebo_inference", po::value<int>()->default_value(0), "1 = run a parallel Learning inference in Frequency mode and discard its result (placebo experiment)")
            ("ads_update_batch", po::value<int>()->default_value(0), "")
            ("workload_path", po::value<std::string>(), "")
            ("tx_delay", po::value<int>()->default_value(0), "micro s")
            ("committer_cnt", po::value<int>()->default_value(10), "committer cnt")
            ("init_dirty", po::value<int>()->default_value(0), "bool")
            ("score_batch", po::value<int>()->default_value(1))
            ("infer_threads", po::value<int>()->default_value(1), "parallel predictors, each with own booster copy and num_threads=1")
            ("operator_enable", po::value<int>()->default_value(0), "1 = keep operator stats thread (legacy); 0 = disable (default)")
            ("data_shard", po::value<int>()->default_value(0), "1 = one data thread per shard (non-batch mode only)")
            ("pk_enable", po::value<int>()->default_value(0), "1 = enable p_k priority scheduling (default 0 = legacy)")
            ("pk_mode", po::value<int>()->default_value(1), "1 = two levels by p_k (level 2/3); 2 = defer (do not enqueue)")
            ("pk_estimator", po::value<int>()->default_value(1), "1 = online smoothed read/write frequency; 2 = offline-fitted LR")
            ("pk_pstar", po::value<double>()->default_value(0.5), "threshold p*: low priority / defer when p_k < p*")
            ("pk_alpha", po::value<double>()->default_value(2.0), "smoothing alpha for the frequency estimator")
            ("pk_levels", po::value<int>()->default_value(8), "number of urgency levels for pk_mode=3 (quantized p_k)")
            ("learing_score_option", po::value<int>()->default_value(0), "b00->DeltaOnly, b01->ScoreWithFreq, b10->LearnWithFreq, b11->LearnScoreWithFreq, 4->DeltaOnly|freq_all")
            ("lr_feature_set", po::value<int>()->default_value(0), "feature-set id: 0=legacy bits; 1=[since_read,deltas,n_within]; 2=+since_any; 3=+log_n_read,density; 4=+n_deltas,has_hist; 5=+edcx4; 6=+write-side")
            ("rx_delay", po::value<int>()->default_value(0), "open-loop pacing at the DATA thread (consumer side); sleep rx_delay us every rx_batch ops after init_training")
            ("rx_batch", po::value<int>()->default_value(10), "ops per pacing sleep for rx_delay (keep single sleep long enough to avoid sleep_for precision loss)")
            ("init_training_cnt", po::value<int>()->default_value(9000000), "")
            ("init_train_pass", po::value<int>()->default_value(1000000), "")
            ("process_cnt", po::value<int>()->default_value(0))
            ("data_scale", po::value<int>()->default_value(2560000), "")
            ("exp_log_name", po::value<string>()->default_value(""), "log name")
            ("result_id", po::value<string>()->default_value(""), "result id")
            ("booster_save", po::value<string>()->default_value(""), "save trained booster to file")
            ("booster_load", po::value<string>()->default_value(""), "load booster from file")
            ("score_dump", po::value<string>()->default_value(""), "dump per-update scores to file")
            ("score_load", po::value<string>()->default_value(""), "load per-update scores from file (no inference)")
            ("skip_init", po::value<int>()->default_value(0), "1=skip init replay+training (loads booster/score)")
            ("op_offset", po::value<int>()->default_value(0), "skip first N ops of the workload file")
            ("send_ops", po::value<int>()->default_value(0), "");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    config::ads_update_batch = vm["ads_update_batch"].as<int>();
    config::tx_delay = vm["tx_delay"].as<int>();
    config::committer_cnt = vm["committer_cnt"].as<int>();
    config::init_dirty = vm["init_dirty"].as<int>();
    config::score_batch = vm["score_batch"].as<int>();
    config::infer_threads = vm["infer_threads"].as<int>();
    config::operator_enable = vm["operator_enable"].as<int>();
    config::data_shard = vm["data_shard"].as<int>();
    config::pk_enable = vm["pk_enable"].as<int>();
    config::lr_feature_set = vm["lr_feature_set"].as<int>();
    config::rx_delay = vm["rx_delay"].as<int>();
    config::rx_batch = vm["rx_batch"].as<int>();
    config::pk_mode = vm["pk_mode"].as<int>();
    config::pk_estimator = vm["pk_estimator"].as<int>();
    config::pk_pstar = vm["pk_pstar"].as<double>();
    config::pk_alpha = vm["pk_alpha"].as<double>();
    config::pk_levels = vm["pk_levels"].as<int>();
    config::learning_score_option = vm["learing_score_option"].as<int>();
    config::init_training_cnt = vm["init_training_cnt"].as<int>();
    config::init_train_pass = vm["init_train_pass"].as<int>();
    config::process_cnt = vm["process_cnt"].as<int>();
    config::data_scale = vm["data_scale"].as<int>();
    config::exp_log_name = vm["exp_log_name"].as<string>();
    config::result_id = vm["result_id"].as<string>();
    config::send_ops = vm["send_ops"].as<int>();
    config::booster_save_path = vm["booster_save"].as<string>();
    config::booster_load_path = vm["booster_load"].as<string>();
    config::score_dump_path = vm["score_dump"].as<string>();
    config::score_load_path = vm["score_load"].as<string>();
    config::skip_init = vm["skip_init"].as<int>();
    config::op_offset = vm["op_offset"].as<int>();
    for (const auto [k, v]: vm) {
        if (vm.count(k))
            cout << k << ": ";
        if (v.value().type() == typeid(string)) {
            cout << v.as<string>() << endl;
        } else if (v.value().type() == typeid(int)) {
            cout << v.as<int>() << endl;
        }
    }
    cout << "---------" << endl;

    int arg_idx = 1;

    int ads_mod = vm["ads_mod"].as<int>();

    Init();

    config::optimize_mode ^= int(config::use_valid_time);
    config::optimize_mode ^= int(config::batch_pre_analyze) << 1;
    cout << "Optimizer: " << config::optimizer_name[config::optimize_mode] << "(" << config::optimizer_ids[config::optimize_mode] << ")" << endl;

    config::new_mode = vm["new_mode"].as<int>();

    config::index_type = vm["index_type"].as<int>();

    config::shard_cnt = vm["shard_cnt"].as<int>();
    config::shard_mode = vm["shard_mode"].as<int>();
    config::mbt_bucket_capacity = vm["mbt_bucket_capacity"].as<int>();
    config::score_buf = vm["score_buf"].as<int>();
    if (vm["out_sched_tput"].as<int>() != 0 && vm["score_buf"].defaulted()) {
        config::score_buf = -1;
    }
    config::out_latency = vm["out_latency"].as<int>();
    config::out_breakdown = vm["out_breakdown"].as<int>();
    config::out_sched_tput = vm["out_sched_tput"].as<int>();
    if (vm.count("tree_cnt")) {
        config::tree_cnt = vm["tree_cnt"].as<int>();
    }

    config::ADS_store_type = vm["ads_store_type"].as<int>();

    if (vm.count("score_window_size"))
    config::score_window_size = vm["score_window_size"].as<int>();
    config::ads_coalesce = vm["ads_coalesce"].as<int>();
    config::freq_ewma_halflife = vm["freq_ewma_halflife"].as<int>();

    config::textbook_preread_size = vm["textbook_preread_size"].as<int>();

    config::max_delta_cnt = vm["max_delta_cnt"].as<int>();
    config::freq_base_file = vm["freq_base_file"].as<std::string>();
    config::placebo_inference = vm["placebo_inference"].as<int>() != 0;

    std::string workload_path = vm["workload_path"].as<std::string>();

    auto workload_name = workload_path.substr(workload_path.rfind('/')+1);
    cout << "workload name: " << workload_name << endl;
    config::workload_file_name = workload_name;
    auto paras = cpputils::string_split(workload_name, "_");
    config::skew = stod((*paras)[1]);
    read_p = stod((*paras)[2]);
    update_p = stod((*paras)[3]);
    insert_p = stod((*paras)[4]);

    rw_offset = stoi((*paras)[6]);

    workloads::init_record_count = stoi((*paras)[8]);
    if (config::data_scale > 0 && config::data_scale < workloads::init_record_count) {
        workloads::init_record_count = config::data_scale;
    }
    ifstream in(workload_path, ios::in);
    string line;
    int line_cnt = 0;
    while (getline(in, line)) {
        ++line_cnt;
    }
    cout << "line_cnt: " << line_cnt << endl;
    workloadsim::file_buf_pc.max_ops = line_cnt;
    workloadsim::file_buf_pc.msgs_buffer_size = line_cnt;
    config::total_ops = line_cnt;

    workloadsim::ads_mod = static_cast<workloadsim::ADS_MOD>(ads_mod);

    cout << "current path: " << fs::current_path().string() << endl;
    cout << "ADS Mode: " << workloadsim::ads_mod<< endl;
    cout << "Policy: ";
    if (config::batch_pre_analyze) {
        cout << config::workload_names2[workloadsim::ads_mod] << endl;
    } else {
        cout << config::maintainer_name[workloadsim::ads_mod] << endl;
    }

    if (config::exp_log_name.length() > 0) {
        notes = config::exp_log_name.substr(config::exp_log_name.rfind('/')+1);
        notes = notes.substr(0, notes.find('.'));
    }
    auto now = std::chrono::system_clock::now();

    std::time_t now_c = std::chrono::system_clock::to_time_t(now);

    stringstream times;
    times << std::put_time(std::localtime(&now_c), "%H:%M");
    log_name = "../../logs/log";
    log_name += "_" + notes + "_";
    log_name += workload_path.substr(workload_path.rfind('/')+1);
    log_name += "_Scale" + to_string(workloads::init_record_count);
    log_name += "_Mode" + std::to_string(workloadsim::ads_mod) + "-" + to_string(config::new_mode);
    log_name += "_Index" + to_string(config::index_type);

    log_name += "_Delay" + to_string(config::tx_delay);
    log_name += "_hist" + to_string(config::score_window_size) + "-" + to_string(config::max_delta_cnt);
    log_name += "_" + times.str();
    process_log_name = log_name + "_process_log.txt";
    config::continuous_log_name = log_name + "_continue_log.txt";
    log_name += ".txt";

    workloadsim::WorkloadSimulator process(workloads::init_record_count);

    process.start_file_workload(workload_path);
};