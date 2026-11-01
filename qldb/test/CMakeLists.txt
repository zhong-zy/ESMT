INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

ADD_LIBRARY(gtest STATIC EXCLUDE_FROM_ALL "gtest/gtest.h" "gtest/gtest-all.cc")
AUX_SOURCE_DIRECTORY(ledger ledger_test_source)

ADD_EXECUTABLE(test_ledger "gtest/gtest_main.cc" ${ledger_test_source})
ADD_DEPENDENCIES(test_ledger ledger)
TARGET_LINK_LIBRARIES(test_ledger gtest ledger)
SET_TARGET_PROPERTIES(test_ledger PROPERTIES LINK_FLAGS "${LINK_FLAGS}")

ADD_TEST(
	NAME test
  COMMAND ${CMAKE_BINARY_DIR}/bin/test_ledger )
ADD_EXECUTABLE(bench_qldb_eth "bench/bench_qldb_eth.cc")
ADD_DEPENDENCIES(bench_qldb_eth ledger)
TARGET_LINK_LIBRARIES(bench_qldb_eth ledger)

ADD_EXECUTABLE(test_btree_page "bench/test_btree_page.cc")
ADD_DEPENDENCIES(test_btree_page ledger)
TARGET_LINK_LIBRARIES(test_btree_page ledger)
