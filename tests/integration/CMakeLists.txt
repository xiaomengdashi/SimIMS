include(GoogleTest)

add_executable(test_full_flow test_full_flow.cpp)

target_link_libraries(test_full_flow PRIVATE
    ims_sip
    ims_sms
    ims_diameter
    ims_rtp
    ims_common
    GTest::gtest_main
    GTest::gmock
)

target_include_directories(test_full_flow PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_full_flow PRIVATE -Wno-character-conversion)
endif()

target_sources(test_full_flow PRIVATE
    ${CMAKE_SOURCE_DIR}/src/p-cscf/pcscf_service.cpp
    ${CMAKE_SOURCE_DIR}/src/i-cscf/icscf_service.cpp
    ${CMAKE_SOURCE_DIR}/src/i-cscf/scscf_selector.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/scscf_service.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/registrar.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/auth_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/digest_credential_store.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/digest_auth_provider.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/ims_aka_auth_provider.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/session_router.cpp
)

gtest_discover_tests(test_full_flow
    PROPERTIES LABELS "integration;e2e"
)
