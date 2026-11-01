set(LIB_SOURCE
        ../include/workloadml/workloadml.h
        workloadml/workloadml.cpp
        ../include/workloads/generator.h
        workloads/generator.cpp
        ../include/workloadsim/processor.h
        workloadsim/processor.cpp
        ../include/workloads/ycsb_workload.h
        workloads/ycsb_workload.cpp
        workloads/workloads.cpp
        ../include/workloads/smallbank_workload.h
        workloads/smallbank_workload.cpp
        workloadsim/data_init.cpp
        ../include/workloadsim/data_init.h
        workloadsim/workload_simulator.cpp
        ../include/workloadsim/workload_simulator.h
        ../include/dbmaintainer/kvdb.h
        ../include/dbmaintainer/db_mbpt.h
        dbmaintainer/kvdb.cpp
        dbmaintainer/db_mbpt.cpp
        ../include/config.h
        config.cpp
        cpputils/random.cpp
        cpputils/sync_ostream.cpp
        cpputils/timer_noinclude.cpp
        workloadml/new_distance.cpp

        dbmaintainer/LETUS/LSVPS.cpp
        dbmaintainer/LETUS/DMMTrie.cpp
        ../include/dbmaintainer/LETUS/common.hpp
        ../include/dbmaintainer/LETUS/DMMTrie.hpp
        ../include/dbmaintainer/LETUS/LSVPS.hpp
        ../include/dbmaintainer/LETUS/VDLS.hpp
)

set(CPP_UTILS
        ../include/cpputils/random.h
        cpputils/random.cpp
        ../include/cpputils/sync_ostream.h
        cpputils/sync_ostream.cpp
        ../include/cpputils/parallel.h
        ../include/cpputils/performance.h
        #        ../include/cpputils/multithread_timer.h
        #        ../include/cpputils/hash.h
        cpputils/SHA256.cpp
        cpputils/string_utils.cpp
)


add_library(workloadsim SHARED ${LIB_SOURCE})
add_library(cpp_utils SHARED ${CPP_UTILS})