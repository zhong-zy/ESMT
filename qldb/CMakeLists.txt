CMAKE_MINIMUM_REQUIRED(VERSION 3.12)

PROJECT(ledgebase)

LIST(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/Modules)

OPTION(LEDGERDB "Use LedgerDB" ON)
OPTION(AMZQLDB "Use QLDB" OFF)
OPTION(SQLLEDGER "Use SQL Ledger" OFF)
OPTION(BUILD_STATIC_LIBS "Build static libraries" OFF)

if(LEDGERDB)
  message(STATUS "Use LedgerDB")
  add_definitions(-DLEDGERDB)
endif()

if(AMZQLDB)
  message(STATUS "Use QLDB")
  add_definitions(-DAMZQLDB)
endif()

if(SQLLEDGER)
  message(STATUS "Use SQL Ledger")
  add_definitions(-DSQLLEDGER)
endif()

OPTION(QDB_PROF "Enable QLDB Set() profiling timestamps" ON)
if(QDB_PROF)
  message(STATUS "QLDB profiling ON")
  add_definitions(-DQDB_PROF)
endif()

OPTION(PROOF_ON_WRITE "Generate proof during Set() (experiment)" OFF)
if(PROOF_ON_WRITE)
  message(STATUS "QLDB: proof generated on write path")
  add_definitions(-DPROOF_ON_WRITE)
endif()
OPTION(QLB_INPLACE "Use page-oriented in-place B+ tree index (experiment)" ON)
OPTION(QLB_FLUSH_MODE "0=mem only, 1=wal, 2=wal+sync page writeback" 1)
if(QLB_INPLACE)
  message(STATUS "QLDB index: page-oriented B+ tree, flush mode ${QLB_FLUSH_MODE}")
  add_definitions(-DQLB_INPLACE -DQLB_FLUSH=${QLB_FLUSH_MODE})
else()
  message(STATUS "QLDB index: original COW B+ tree")
endif()

SET(BOOST_COMPONENT "filesystem")
LIST(APPEND BOOST_COMPONENT "program_options")
LIST(APPEND BOOST_COMPONENT "thread")

# ---[ Dependencies
include(cmake/Dependencies.cmake)

enable_testing()
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -O2 -pthread -lssl -Wall -Wno-unused-function -fPIC -lzstd -lsnappy -llz4 -lbz2 -lz /usr/local/lib/libcryptopp.a")

MESSAGE(STATUS "${PROTOBUF_PROTOC_EXECUTABLE}")
LIST(APPEND LEDGEBASE_INCLUDE_DIR "${CMAKE_SOURCE_DIR}")
LIST(APPEND LEDGEBASE_INCLUDE_DIR "${CMAKE_BINARY_DIR}")
INCLUDE_DIRECTORIES(${LEDGEBASE_INCLUDE_DIR})
MESSAGE(STATUS "include: ${LEDGEBASE_INCLUDE_DIR}")

SET(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)
SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

ADD_SUBDIRECTORY(ledger)
ADD_SUBDIRECTORY(distributed)
ADD_SUBDIRECTORY(test)

SET(NONEXE_PERMISSION
    OWNER_READ
    OWNER_WRITE
    GROUP_READ
    WORLD_READ)

SET(EXE_PERMISSION
    ${NONEXE_PERMISSION}
    OWNER_EXECUTE
    GROUP_EXECUTE
    WORLD_EXECUTE)

INSTALL(DIRECTORY ${LIBRARY_OUTPUT_PATH} DESTINATION .
        FILE_PERMISSIONS ${EXE_PERMISSION}
        FILES_MATCHING PATTERN "*.so")
INSTALL(DIRECTORY ${LIBRARY_OUTPUT_PATH} DESTINATION .
        FILE_PERMISSIONS ${NONEXE_PERMISSION}
        FILES_MATCHING PATTERN "*.a")
INSTALL(DIRECTORY ${EXECUTABLE_OUTPUT_PATH} DESTINATION .
        FILE_PERMISSIONS ${EXE_PERMISSION})
INSTALL(DIRECTORY ${CMAKE_SOURCE_DIR}/distributed
                  ${CMAKE_SOURCE_DIR}/ledger
        DESTINATION include/LedgerDatabase
        FILES_MATCHING PATTERN "*.h*")
INSTALL(DIRECTORY ${CMAKE_BINARY_DIR}/distributed/proto
        DESTINATION include/LedgerDatabase/distributed)
