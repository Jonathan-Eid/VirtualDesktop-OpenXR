cmake_minimum_required(VERSION 3.21)
project(vdxr_ultimate_runtime_tests LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The Ultimate tracker IPC tests exercise Windows local shared memory")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)

enable_testing()
add_executable(ultimate-ipc-test ipc_test.cpp)
add_test(NAME ultimate-ipc COMMAND ultimate-ipc-test)
set_tests_properties(ultimate-ipc PROPERTIES RUN_SERIAL TRUE)

set(VDXR_RUNTIME_DLL "" CACHE FILEPATH "Built x64 Nightfall VDXR runtime DLL for the synthetic role test")
if(VDXR_RUNTIME_DLL)
    set(VDXR_OPENXR_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../external/OpenXR-SDK/include" CACHE PATH "OpenXR include directory")
    add_executable(ultimate-runtime-test runtime_test.cpp)
    target_include_directories(ultimate-runtime-test PRIVATE "${VDXR_OPENXR_INCLUDE}")
    add_test(NAME ultimate-runtime COMMAND ultimate-runtime-test "${VDXR_RUNTIME_DLL}")
    set_tests_properties(ultimate-runtime PROPERTIES RUN_SERIAL TRUE)
endif()
