add_executable(test_smoke smoke_test.cpp)
add_executable(test_crypt_hex crypt_hex_test.cpp)
add_executable(test_crypt_ecdh crypt_ecdh_test.cpp)
add_executable(test_crypt_aes crypt_aes_test.cpp)


target_link_libraries(test_smoke PRIVATE logger GTest::gtest GTest::gtest_main)
target_link_libraries(test_crypt_hex PRIVATE logger GTest::gtest GTest::gtest_main)
target_link_libraries(test_crypt_ecdh PRIVATE logger GTest::gtest GTest::gtest_main)
target_link_libraries(test_crypt_aes PRIVATE logger GTest::gtest GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(test_smoke)
gtest_discover_tests(test_crypt_hex)
gtest_discover_tests(test_crypt_ecdh)
gtest_discover_tests(test_crypt_aes)