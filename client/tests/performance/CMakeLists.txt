cmake_minimum_required(VERSION 3.21)

find_package(benchmark REQUIRED)

add_executable(client_latency_benchmark
    LatencyBenchmark.cpp
)

target_include_directories(client_latency_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/utils
)

target_link_libraries(client_latency_benchmark PRIVATE
    benchmark::benchmark
)

set_target_properties(client_latency_benchmark PROPERTIES
    CXX_STANDARD 20
)
