add_executable(decoder decode.cpp decode_formatter.cpp)
target_link_libraries(decoder PRIVATE logger)