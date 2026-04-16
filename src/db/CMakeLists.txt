add_library(ims_db
    mongo_client.cpp
    subscriber_codec.cpp
    mongo_subscriber_repository.cpp
)

target_include_directories(ims_db PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ims_db PUBLIC
    ims_common
    ims_sip
)

find_package(PkgConfig REQUIRED)
# Support both mongo-c-driver 1.x and 2.x
pkg_check_modules(LIBMONGOC IMPORTED_TARGET libmongoc-1.0)
if(NOT LIBMONGOC_FOUND)
    pkg_check_modules(LIBMONGOC REQUIRED IMPORTED_TARGET mongoc2)
endif()

pkg_check_modules(LIBBSON IMPORTED_TARGET libbson-1.0)
if(NOT LIBBSON_FOUND)
    pkg_check_modules(LIBBSON REQUIRED IMPORTED_TARGET bson2)
endif()

target_link_libraries(ims_db PUBLIC
    PkgConfig::LIBMONGOC
    PkgConfig::LIBBSON
)
