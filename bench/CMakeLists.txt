find_package(spdlog CONFIG REQUIRED)

add_executable(bench_logger bench_logger.cpp)

target_link_libraries(bench_logger PRIVATE logger benchmark::benchmark benchmark::benchmark_main spdlog::spdlog)

