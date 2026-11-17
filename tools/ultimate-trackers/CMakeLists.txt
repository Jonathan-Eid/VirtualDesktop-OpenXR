cmake_minimum_required(VERSION 3.21)
project(ultimate_tracker_probe LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ultimate-tracker-probe uses the Windows SetupAPI and HID APIs")
endif()

include(CTest)

find_package(Threads REQUIRED)

add_library(ultimate-hid-source STATIC ultimate_hid_source.cpp)
target_compile_features(ultimate-hid-source PUBLIC cxx_std_17)
target_compile_definitions(ultimate-hid-source PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(ultimate-hid-source PRIVATE /W4 /WX /permissive-)
target_include_directories(ultimate-hid-source PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ultimate-hid-source PUBLIC setupapi hid Threads::Threads)

add_executable(ultimate-source-probe ultimate_source_probe.cpp)
target_compile_features(ultimate-source-probe PRIVATE cxx_std_17)
target_compile_definitions(ultimate-source-probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(ultimate-source-probe PRIVATE /W4 /WX /permissive-)
target_link_libraries(ultimate-source-probe PRIVATE ultimate-hid-source)

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

    add_executable(ultimate-hid-source-tests ultimate_hid_source_tests.cpp)
    target_compile_features(ultimate-hid-source-tests PRIVATE cxx_std_17)
    target_compile_options(ultimate-hid-source-tests PRIVATE /W4 /WX /permissive-)
    target_link_libraries(ultimate-hid-source-tests PRIVATE ultimate-hid-source)
    add_test(NAME ultimate-hid-source-fixtures COMMAND ultimate-hid-source-tests)
endif()
