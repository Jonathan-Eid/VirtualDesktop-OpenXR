cmake_minimum_required(VERSION 3.21)
project(ultimate_tracker_probe LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ultimate-tracker-probe uses the Windows SetupAPI and HID APIs")
endif()

include(CTest)

add_executable(ultimate-tracker-probe probe.cpp)
target_compile_features(ultimate-tracker-probe PRIVATE cxx_std_17)
target_compile_definitions(ultimate-tracker-probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(ultimate-tracker-probe PRIVATE /W4 /WX /permissive-)
target_link_libraries(ultimate-tracker-probe PRIVATE setupapi hid)

if(BUILD_TESTING)
    add_executable(ultimate-tracker-probe-tests probe_core_tests.cpp)
    target_compile_features(ultimate-tracker-probe-tests PRIVATE cxx_std_17)
    target_compile_options(ultimate-tracker-probe-tests PRIVATE /W4 /WX /permissive-)
    add_test(NAME ultimate-tracker-probe-fixtures COMMAND ultimate-tracker-probe-tests)
endif()
