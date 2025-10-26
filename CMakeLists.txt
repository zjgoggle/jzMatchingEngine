cmake_minimum_required( VERSION 3.13 )
project(JzMatchingEngine)

message("Building JzMatchingEngine ${ARCHITECTURE}-${CMAKE_BUILD_TYPE}")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

enable_testing()

set(CMAKE_CXX_STANDARD 20) # especially for abseil
add_subdirectory(${CMAKE_SOURCE_DIR}/external/abseil-cpp)

add_subdirectory(src)
add_subdirectory(test/unit)
add_subdirectory(test/func)