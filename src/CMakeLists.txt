cmake_minimum_required( VERSION 3.13 )
project(JzMatchingEngineMain)
set(targetname JzMatchingEngineMain)

file(GLOB SRC
    *.cpp)

add_executable(${targetname} ${SRC})
target_compile_features( ${targetname} PUBLIC cxx_std_20 )
