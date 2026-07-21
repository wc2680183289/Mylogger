add_executable(test_mmap test_mmap.cpp)
add_executable(test_space test_space.cpp)
add_executable(test_fmt test_fmt.cpp)
add_executable(test_threadpool test_threadpool.cpp)

target_link_libraries(test_mmap PRIVATE logger)
target_link_libraries(test_space PRIVATE logger)
target_link_libraries(test_fmt PRIVATE logger)
target_link_libraries(test_threadpool PRIVATE logger)