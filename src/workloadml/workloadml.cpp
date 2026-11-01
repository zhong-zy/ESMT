
#include "workloadml/workloadml.h"
#include <iostream>

using namespace std;

namespace workloadml {
    uint time_window_size = 10000;
    uint8_t max_n_past_timestamps = 32;
    uint max_n_past_distances = 31;

    void BoostData::show(std::string &&name) {
        cout << "name=" << name << endl;
        cout << "labels.size()=" << labels.size() << endl;
        cout << "&labels=" << &labels << endl;
        cout << "labels[0]=" << labels[0] << endl;
        cout << "&labels[0]=" << &labels[0] << endl;
    }
}