cmake_minimum_required( VERSION 3.13 )
project(JzMatchingEngine-UnitTest)
set(targetname JzMatchingEngine-UnitTest)

file(GLOB SRC
    *.cpp)

add_executable(${targetname} ${SRC})
target_compile_features(${targetname} PUBLIC cxx_std_20)
target_include_directories(${targetname} SYSTEM PUBLIC 
    ${CMAKE_SOURCE_DIR}/src
)
# set_source_files_properties(JzMatchingEngine-test.cpp PROPERTIES COMPILE_DEFINITIONS "TEST_CONFIG_IMPLEMENT_MAIN=1")

add_test(NAME ${targetname}  COMMAND $<TARGET_FILE:${targetname}>)