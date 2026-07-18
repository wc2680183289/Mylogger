add_library(logger STATIC
    mmap/mmap_aux.cpp
    mmap/mmap_linux.cpp
    utils/file_util.cc
    utils/sys_util.cc
    utils/sys_util_linux.cc
)

target_include_directories(logger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/mmap
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
)

target_link_libraries(logger PUBLIC pthread)
