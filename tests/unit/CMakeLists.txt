include(GoogleTest)

# Include directories for test mocks
set(MOCK_DIR ${CMAKE_SOURCE_DIR}/tests/mocks)

# SIP message test
add_executable(test_sip_message test_sip_message.cpp)
target_link_libraries(test_sip_message PRIVATE ims_sip GTest::gtest_main)
gtest_discover_tests(test_sip_message)

# DNS resolver test
add_executable(test_dns_resolver test_dns_resolver.cpp)
target_link_libraries(test_dns_resolver PRIVATE ims_dns GTest::gtest_main)
gtest_discover_tests(test_dns_resolver)

# Bencode test
add_executable(test_bencode test_bencode.cpp)
target_link_libraries(test_bencode PRIVATE ims_rtp GTest::gtest_main)
gtest_discover_tests(test_bencode)

# Config test
add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE ims_common GTest::gtest_main)
gtest_discover_tests(test_config)

# Diameter Cx test (uses mocks)
add_executable(test_diameter_cx test_diameter_cx.cpp)
target_link_libraries(test_diameter_cx PRIVATE ims_diameter ims_common GTest::gtest_main GTest::gmock)
target_include_directories(test_diameter_cx PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(test_diameter_cx)

# Registration store test (uses mocks)
add_executable(test_registrar test_registrar.cpp)
target_link_libraries(test_registrar PRIVATE ims_common GTest::gtest_main GTest::gmock)
target_include_directories(test_registrar PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(test_registrar)

# S-CSCF auth manager test
add_executable(test_auth_manager
    test_auth_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/auth_manager.cpp
)
target_link_libraries(test_auth_manager PRIVATE ims_common GTest::gtest_main)
target_include_directories(test_auth_manager PRIVATE ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(test_auth_manager)
