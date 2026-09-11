add_subdirectory(third_party/fmt)
add_subdirectory(third_party/zlib)
add_subdirectory(third_party/zstd/build/cmake zstd)
add_subdirectory(third_party/protobuf)

add_library(logger STATIC
    mmap/mmap_aux.cpp
    mmap/mmap_linux.cpp
    utils/file_util.cc
    utils/sys_util.cc
    utils/sys_util_linux.cc
    context/thread_pool.cpp
    context/executor.cpp
    context/context.cpp
    sinks/console_sink.cpp
    sinks/effective_sink.cpp
    formatter/default_formatter.cpp
    formatter/effective_formatter.cpp
    log_handle.cpp
    log_factory.cpp
    log_msg.cpp
    compress/zlib_compress.cpp
    compress/zstd_compress.cpp
    crypt/crypt.cpp
    crypt/aes_crypt.cpp
    ${CMAKE_BINARY_DIR}/effective_msg.pb.cc
)

target_include_directories(logger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/mmap
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/context
    ${CMAKE_CURRENT_SOURCE_DIR}/sinks
    ${CMAKE_CURRENT_SOURCE_DIR}/formatter
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/compress
    ${CMAKE_CURRENT_SOURCE_DIR}/crypt
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/protobuf
    ${CMAKE_BINARY_DIR}
)

find_package(OpenSSL REQUIRED)

target_compile_definitions(logger PUBLIC ENABLE_LOG)

target_link_libraries(logger PUBLIC pthread fmt::fmt zlib libzstd_static OpenSSL::Crypto protobuf)
