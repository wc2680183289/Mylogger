add_subdirectory(third_party/fmt)

add_library(logger STATIC
    mmap/mmap_aux.cpp
    mmap/mmap_linux.cpp
    utils/file_util.cc
    utils/sys_util.cc
    utils/sys_util_linux.cc
    context/thread_pool.cpp
    context/executor.cpp
)

target_include_directories(logger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/mmap
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/context
)

target_link_libraries(logger PUBLIC pthread fmt::fmt)
