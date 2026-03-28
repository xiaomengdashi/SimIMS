include(GoogleTest)

function(ims_configure_test_target target_name)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target_name} PRIVATE -Wno-character-conversion)
    endif()
endfunction()

# Include directories for test mocks
set(MOCK_DIR ${CMAKE_SOURCE_DIR}/tests/mocks)

# SIP message test
add_executable(test_sip_message test_sip_message.cpp)
target_link_libraries(test_sip_message PRIVATE ims_sip GTest::gtest_main)
ims_configure_test_target(test_sip_message)
gtest_discover_tests(test_sip_message)

# ProxyCore test
add_executable(test_proxy_core test_proxy_core.cpp)
target_link_libraries(test_proxy_core PRIVATE ims_sip ims_common GTest::gtest_main)
ims_configure_test_target(test_proxy_core)
gtest_discover_tests(test_proxy_core)

# eXosip adapter test
add_executable(test_exosip_adapter test_exosip_adapter.cpp)
target_link_libraries(test_exosip_adapter PRIVATE ims_sip GTest::gtest_main)
ims_configure_test_target(test_exosip_adapter)
gtest_discover_tests(test_exosip_adapter)

# DNS resolver test
add_executable(test_dns_resolver test_dns_resolver.cpp)
target_link_libraries(test_dns_resolver PRIVATE ims_dns GTest::gtest_main)
ims_configure_test_target(test_dns_resolver)
gtest_discover_tests(test_dns_resolver)

# Bencode test
add_executable(test_bencode test_bencode.cpp)
target_link_libraries(test_bencode PRIVATE ims_rtp GTest::gtest_main)
ims_configure_test_target(test_bencode)
gtest_discover_tests(test_bencode)

# Config test
add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE ims_common GTest::gtest_main)
ims_configure_test_target(test_config)
gtest_discover_tests(test_config)

# Diameter Cx test (uses mocks)
add_executable(test_diameter_cx test_diameter_cx.cpp)
target_link_libraries(test_diameter_cx PRIVATE ims_diameter ims_common GTest::gtest_main GTest::gmock)
target_include_directories(test_diameter_cx PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/src)
ims_configure_test_target(test_diameter_cx)
gtest_discover_tests(test_diameter_cx)

# Registration store test (uses mocks)
add_executable(test_registrar test_registrar.cpp)
target_link_libraries(test_registrar PRIVATE ims_common ims_sip GTest::gtest_main GTest::gmock)
target_include_directories(test_registrar PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/src)
ims_configure_test_target(test_registrar)
gtest_discover_tests(test_registrar)

# S-CSCF auth manager test
add_executable(test_auth_manager
    test_auth_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/auth_manager.cpp
)
target_link_libraries(test_auth_manager PRIVATE ims_sip ims_common GTest::gtest_main)
target_include_directories(test_auth_manager PRIVATE ${CMAKE_SOURCE_DIR}/src)
ims_configure_test_target(test_auth_manager)
gtest_discover_tests(test_auth_manager)

add_executable(test_auth_providers
    test_auth_providers.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/auth_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/digest_credential_store.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/digest_auth_provider.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/ims_aka_auth_provider.cpp
)
target_link_libraries(test_auth_providers PRIVATE ims_sip ims_common ims_diameter GTest::gtest_main GTest::gmock)
target_include_directories(test_auth_providers PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/src)
ims_configure_test_target(test_auth_providers)
gtest_discover_tests(test_auth_providers)

# S-CSCF service test
add_executable(test_scscf_service
    test_scscf_service.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/scscf_service.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/registrar.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/auth_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/digest_credential_store.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/digest_auth_provider.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/ims_aka_auth_provider.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/session_router.cpp
)
target_link_libraries(test_scscf_service PRIVATE ims_dns ims_diameter GTest::gtest_main GTest::gmock)
target_include_directories(test_scscf_service PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/src)
ims_configure_test_target(test_scscf_service)
gtest_discover_tests(test_scscf_service)

# P-CSCF service test
add_executable(test_pcscf_service
    test_pcscf_service.cpp
    ${CMAKE_SOURCE_DIR}/src/p-cscf/pcscf_service.cpp
)
target_link_libraries(test_pcscf_service PRIVATE
    ims_sip
    ims_diameter
    ims_rtp
    ims_common
    GTest::gtest_main
    GTest::gmock
)
target_include_directories(test_pcscf_service PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/src)
ims_configure_test_target(test_pcscf_service)
gtest_discover_tests(test_pcscf_service)
