cmake_minimum_required(VERSION 3.22)
project(Atelier VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ATELIER_BUILD_PLUGIN "Build the ATELIER VST3 plugin" ON)
option(SONIC_BUILD_TESTS "Build core DSP tests" ON)

add_subdirectory(core)

if(SONIC_BUILD_TESTS)
    enable_testing()
    add_executable(sonic_core_tests tests/CoreTests.cpp)
    target_link_libraries(sonic_core_tests PRIVATE sonic_core)
    add_test(NAME core_dsp COMMAND sonic_core_tests)
endif()

if(ATELIER_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()
