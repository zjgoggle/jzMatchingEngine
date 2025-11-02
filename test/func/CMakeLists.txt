cmake_minimum_required( VERSION 3.13 )
project(NasdaqITCHReader)
set(targetname NasdaqITCHReader)

file(GLOB SRC
    *.cpp)

add_executable(${targetname} ${SRC})
target_compile_features(${targetname} PUBLIC cxx_std_20)
target_include_directories(${targetname} SYSTEM PUBLIC 
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/pfr/include
)
if (TEST_BENCH_HASH_MAP)
    target_compile_definitions(${targetname} PUBLIC TEST_BENCH_HASH_MAP=1)
    target_link_libraries(${targetname} absl::flat_hash_map)
endif()
#=================================================
#    SimpleMatchingEngineMain-Test
#=================================================
set(targetname NasdaqITCHReader-Test)

add_executable(${targetname} ${SRC})
target_compile_features(${targetname} PUBLIC cxx_std_20)
target_include_directories(${targetname} SYSTEM PUBLIC 
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/pfr/include
)
target_compile_definitions(${targetname} PRIVATE TEST_CONFIG_IMPLEMENT_MAIN=1) # convert to a test
if (TEST_BENCH_HASH_MAP)
    target_compile_definitions(${targetname} PUBLIC TEST_BENCH_HASH_MAP=1)
    target_link_libraries(${targetname} absl::flat_hash_map)
endif()

# add_test(NAME ${targetname}  COMMAND $<TARGET_FILE:${targetname}>) Do not run test which needs external data file
