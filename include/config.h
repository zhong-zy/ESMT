
#ifndef INDEX_MAINTAIN_CONFIG_H
#define INDEX_MAINTAIN_CONFIG_H

#include <string>

#define VALUE_RANGE 8388608

#define PRINT_LOG

namespace config {
    extern bool process;
    extern bool learning;
    extern bool batch_pre_analyze;
    extern int pre_analyze_batch;
    extern int init_training_cnt;

    extern std::string booster_save_path, booster_load_path;
    extern std::string score_dump_path, score_load_path;
    extern int skip_init;
    extern int op_offset;
    extern bool wr_both;
    extern double skew;

    extern int ads_update_batch;
    extern std::string maintainer_name[];
    extern bool use_valid_time;
    extern int optimize_mode;
    extern std::string optimizer_name[];
    extern std::string optimizer_ids[];
    extern std::string workload_names2[];
    extern int maintainer_ids[];
    extern int total_ops;

    extern int ads_queue_limit_height;
    extern int ads_coalesce;

    extern std::string workload_file_name;

    extern int textbook_preread_size;
    extern int score_window_size;
    extern int new_mode;
    extern std::string new_mode_names[];
    extern int max_delta_cnt;
    extern int cut_off_distance;

    extern std::string freq_base_file;

    extern int freq_ewma_halflife;

    extern bool placebo_inference;
    extern int init_train_pass;

    extern int index_type;
    extern int shrubs_type;
    extern int shrub_min_size;
    extern int ADS_store_type;

    extern int tree_cnt;

    extern int shard_cnt;
    extern int shard_mode;

    extern int mbt_bucket_capacity;

    extern int out_latency;
    extern int out_breakdown;
    extern int out_sched_tput;

    extern int tx_delay;
    extern int committer_cnt;
    extern bool init_dirty;

    extern std::string continuous_log_name;
    extern int continuous_log_interval;

    extern int score_batch;

    extern int infer_threads;

    extern int operator_enable;

    extern int data_shard;

    extern int pk_enable;
    extern int pk_mode;
    extern int pk_estimator;
    extern double pk_pstar;
    extern double pk_alpha;

    extern int pk_levels;

    extern int learning_score_option;

    extern int rx_delay;
    extern int rx_batch;

    extern int lr_feature_set;

    extern int data_scale;

    #define COW
    inline int process_cnt;

    inline std::string exp_log_name;
    inline std::string result_id;

    inline int send_ops;
}

extern std::string log_name;
extern std::string exp_results_name;
extern std::string notes;
extern std::string process_log_name;

extern double read_p;
extern double update_p;
extern double insert_p;
extern int window_size;
extern int64_t rw_offset;

void Init();

#endif
