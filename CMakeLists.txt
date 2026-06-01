cmake_minimum_required(VERSION 3.22)
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
project(ims VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output executables to bin directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Find packages
find_package(Boost REQUIRED COMPONENTS system)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)
if(NOT TARGET yaml-cpp::yaml-cpp AND TARGET yaml-cpp)
    add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
endif()
find_package(GTest REQUIRED)
find_package(OpenSSL REQUIRED)

# pkg-config based dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBOSIP2 REQUIRED IMPORTED_TARGET libosip2)
pkg_check_modules(LIBCARES REQUIRED IMPORTED_TARGET libcares)

# Add subdirectories
add_subdirectory(src/common)
add_subdirectory(src/crypt)
add_subdirectory(src/sip)
add_subdirectory(src/sms)
add_subdirectory(src/dns)
add_subdirectory(src/db)
add_subdirectory(src/diameter)
add_subdirectory(src/rtp)
add_subdirectory(src/p-cscf)
add_subdirectory(src/i-cscf)
add_subdirectory(src/s-cscf)
add_subdirectory(src/smsc)
add_subdirectory(src/allinone)

# Testing
enable_testing()
add_subdirectory(tests/unit)
add_subdirectory(tests/integration)

add_test(
    NAME BaresipInviteIntegrationTest
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/test_baresip_invite.sh
)
set_tests_properties(BaresipInviteIntegrationTest PROPERTIES
    LABELS integration
    SKIP_RETURN_CODE 77
)

# Install targets
install(TARGETS ims_pcscf ims_icscf ims_scscf ims_smsc ims_allinone
    RUNTIME DESTINATION bin
)
install(DIRECTORY config/ DESTINATION etc/ims)
