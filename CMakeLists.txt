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

# pkg-config based dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBOSIP2 REQUIRED IMPORTED_TARGET libosip2)
pkg_check_modules(LIBEXOSIP2 QUIET IMPORTED_TARGET libeXosip2)
pkg_check_modules(LIBCARES REQUIRED IMPORTED_TARGET libcares)

if (TARGET PkgConfig::LIBEXOSIP2)
    set(IMS_EXOSIP_TARGET PkgConfig::LIBEXOSIP2)
else()
    find_path(LIBEXOSIP2_INCLUDE_DIR
        NAMES eXosip2/eXosip.h
        HINTS /usr/local/include /usr/include
    )
    find_library(LIBEXOSIP2_LIBRARY
        NAMES eXosip2 libeXosip2
        HINTS /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu
    )

    if (NOT LIBEXOSIP2_INCLUDE_DIR OR NOT LIBEXOSIP2_LIBRARY)
        message(FATAL_ERROR "libeXosip2 not found via pkg-config or manual lookup")
    endif()

    add_library(ims_exosip2 UNKNOWN IMPORTED)
    set_target_properties(ims_exosip2 PROPERTIES
        IMPORTED_LOCATION "${LIBEXOSIP2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBEXOSIP2_INCLUDE_DIR}"
    )
    set(IMS_EXOSIP_TARGET ims_exosip2)
endif()

# Add subdirectories
add_subdirectory(src/common)
add_subdirectory(src/sip)
add_subdirectory(src/dns)
add_subdirectory(src/diameter)
add_subdirectory(src/rtp)
add_subdirectory(src/p-cscf)
add_subdirectory(src/i-cscf)
add_subdirectory(src/s-cscf)
add_subdirectory(src/allinone)

# Testing
enable_testing()
add_subdirectory(tests/unit)

add_test(
    NAME BaresipInviteIntegrationTest
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/test_baresip_invite.sh
)
set_tests_properties(BaresipInviteIntegrationTest PROPERTIES
    LABELS integration
)

# Install targets
install(TARGETS ims_pcscf ims_icscf ims_scscf ims_allinone
    RUNTIME DESTINATION bin
)
install(DIRECTORY config/ DESTINATION etc/ims)
