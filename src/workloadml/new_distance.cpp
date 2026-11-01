
#include "workloadml/new_distance.h"

namespace workloadml {

std::string training_params_override;

int train_label_mode = 0;
int train_label_horizon = 10000;

int inference_num_threads = 0;

    const char *training_params = "boosting=gbdt\n"
                            "objective=regression\n"
                            "num_iterations=32\n"

                            "num_leaves=32\n"

                            "num_threads=4\n"
                            "feature_fraction=0.8\n"
                            "bagging_freq=5\n"
                            "bagging_fraction=0.8\n"
                            "learning_rate=0.1\n"
                            "verbosity=0\n";

}