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
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBMONGOC REQUIRED IMPORTED_TARGET libmongoc-1.0)
pkg_check_modules(LIBBSON REQUIRED IMPORTED_TARGET libbson-1.0)

target_link_libraries(ims_db PUBLIC
    PkgConfig::LIBMONGOC
    PkgConfig::LIBBSON
)
