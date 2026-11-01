cmake_minimum_required(VERSION 3.22)
project(SHRUBS_LIKE_INDEX LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -g -gdwarf-3 -fpermissive")

link_directories(/usr/local/lib)

option(RAS "Run After Score: wait for the scoring thread before the measurement window" ON)
if(RAS)
    add_compile_definitions(RUN_AFTER_SCORE)
endif()

set(MBPT_INC src/dbmaintainer/mbpt/mbpt.cpp)
set(BPT_INC src/dbmaintainer/mbpt/bpt.cpp)

include_directories(include)
find_package(Boost REQUIRED COMPONENTS program_options)

find_package(OpenSSL 1.1 REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

set(MBPT_SRC
        src/dbmaintainer/mbpt/bpt.cpp
        src/dbmaintainer/mbpt/mbpt.cpp
)
add_library(mbpt SHARED ${MBPT_SRC})

set(WORKLOADSIM_HEADER_PATH ${PROJECT_SOURCE_DIR}/include/workloadsim)
add_subdirectory(src)
add_subdirectory(runnable)
