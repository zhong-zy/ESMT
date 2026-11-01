set(LIBS workloadsim cpp_utils mbpt rocksdb dl z zstd bz2 lz4 snappy _lightgbm
         OpenSSL::SSL OpenSSL::Crypto)

add_executable(main main.cpp)
target_link_libraries(main ${LIBS} Boost::program_options)

add_executable(wiki_workload_gen wiki_workload_gen.cpp)
target_link_libraries(wiki_workload_gen ${LIBS} Boost::program_options)

