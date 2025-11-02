cmake_minimum_required( VERSION 3.13 )
project(JzMatchingEngine)

message("Building JzMatchingEngine ${ARCHITECTURE}-${CMAKE_BUILD_TYPE}")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

enable_testing()

option(TEST_BENCH_HASH_MAP "Enable or disable build bench hashmap" OFF)
if (TEST_BENCH_HASH_MAP)
    set(CMAKE_CXX_STANDARD 20) # especially for abseil
    # add_subdirectory(${CMAKE_SOURCE_DIR}/external/abseil-cpp)
endif()

add_subdirectory(src)
# add_subdirectory(test/unit)
add_subdirectory(test/func)