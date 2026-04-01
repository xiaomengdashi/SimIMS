add_library(ims_crypt
    milenage.cpp
)

target_include_directories(ims_crypt PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ims_crypt PUBLIC
    ims_common
    OpenSSL::Crypto
)
