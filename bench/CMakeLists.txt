find_package(spdlog CONFIG REQUIRED)

add_executable(bench_logger bench_logger.cpp)
add_executable(bench_vs_spdlog bench_vs_spdlog.cpp)

target_link_libraries(bench_logger PRIVATE logger benchmark::benchmark benchmark::benchmark_main spdlog::spdlog)
target_link_libraries(bench_vs_spdlog PRIVATE logger spdlog::spdlog)

