cmake_minimum_required( VERSION 3.13 )
project(SimpleMatchingEngineMain)
set(targetname SimpleMatchingEngineMain)

file(GLOB SRC
    *.cpp)

add_executable(${targetname} ${SRC})
target_compile_features(${targetname} PUBLIC cxx_std_20)

#=================================================
#    SimpleMatchingEngineMain-Test
#=================================================
set(targetname SimpleMatchingEngineMain-Test)
add_executable(${targetname} ${SRC})
target_compile_features(${targetname} PUBLIC cxx_std_20)
target_compile_definitions(${targetname} PRIVATE TEST_CONFIG_IMPLEMENT_MAIN=1) # convert to a test

add_test(NAME ${targetname}  COMMAND $<TARGET_FILE:${targetname}>)