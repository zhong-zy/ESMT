
#include "config.h"
#include "workloadsim/processor.h"
#include "workloadsim/data_init.h"
#include "workloadsim/workload_simulator.h"
#include "cpputils/random.h"

string log_name;

string notes = "experiment";
string config::workload_file_name;

string exp_results_name = "results/" + notes + ".tsv";

string process_log_name;

#ifdef MODEL_WITH_FREQ
#define learning_name "Learning(+Freq)"
#else
#define learning_name "Learning"
#endif

string config::maintainer_name[] = {"Sequential", "Lazy", "Batch", "ESMT (Frequency)", "ESMT (Learning)", "ESMT (Sequential)", "n/a", "n/a", "n/a", "ESMT", "n/a"};
int config::maintainer_ids[] = {1, 2, 0, 4, 5, 3, 7, 6, 8, 9, 10};
bool config::use_valid_time = false;
int config::optimize_mode = 0;
string config::optimizer_name[] = {"None", "Valid Time", "Batch Plan", "Batch Plan + Valid Time"};
string config::optimizer_ids[] = {"00", "01", "10", "11"};
string config::workload_names2[] = {"Err0", "on-demand+batch-analysis", "Err2", "on-demand+batch-analysis+freq-order", "on-demand+batch-analysis+learn-order", "on-demand+batch-analysis+time-order", "Err6", "Err7", "Err8"};

double config::skew;
double read_p;
double update_p;
double insert_p;
int window_size;
int64_t rw_offset = 0;
int config::total_ops;

int config::ads_queue_limit_height = 8;
int config::ads_coalesce = 0;

bool config::process = true;

std::string config::booster_save_path = "";
std::string config::booster_load_path = "";
std::string config::score_dump_path = "";
std::string config::score_load_path = "";
int config::skip_init = 0;
int config::op_offset = 0;
bool config::learning = false;
bool config::batch_pre_analyze = false;
int config::pre_analyze_batch = 0;
int config::ads_update_batch = 500;

int config::init_training_cnt = 10100000;
int config::init_train_pass = 10000000;

bool config::wr_both = true;

int config::textbook_preread_size = 0;
int config::score_window_size = 100;
int config::new_mode = 0;
string config::new_mode_names[] = {"none", "frequency", "distance", "distance-learning"};
int config::max_delta_cnt = 32;
int config::cut_off_distance = 0;

string config::freq_base_file;

int config::freq_ewma_halflife = 0;

bool config::placebo_inference = false;

int config::index_type = 0;
int config::shrubs_type = 0;
int config::shrub_min_size = 4;
int config::ADS_store_type = 0;

int config::tree_cnt = 1;

int config::shard_cnt = 1;
int config::shard_mode = 0;

int config::mbt_bucket_capacity = 65536;

int config::score_buf = 1;

int config::out_latency = 0;
int config::out_breakdown = 0;
int config::out_sched_tput = 0;

int config::tx_delay = 0;
int config::committer_cnt = 1;
bool config::init_dirty = false;

string config::continuous_log_name;
int config::continuous_log_interval = 10000;

int config::score_batch = 1;
int config::infer_threads = 1;

int config::operator_enable = 0;
int config::data_shard = 0;

int config::pk_enable = 0;
int config::pk_mode = 1;
int config::pk_estimator = 1;
double config::pk_pstar = 0.5;
double config::pk_alpha = 2.0;
int config::pk_levels = 8;

int config::learning_score_option = 0;

int config::rx_delay = 0;
int config::rx_batch = 10;

int config::lr_feature_set = 0;

int config::data_scale = 0;

void Init() {

    workloads::rand_mode = cpputils::SetSeed;
    workloads::ycsb::use_pre_gen_values = true;
    cpputils::setted_seed = 100;
    workloadsim::pure_batch_size = 50;

    workloads::init_record_count = VALUE_RANGE;

    workloadsim::file_buf_pc.max_ops = 1000000;
    workloadsim::file_buf_pc.msgs_buffer_size = 1000000;
    workloadsim::pre_gen_workload = true;
    window_size = 10000;

}
