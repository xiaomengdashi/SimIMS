add_library(ims_dns
    resolver.cpp
)

target_include_directories(ims_dns PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(ims_dns PUBLIC
    ims_common
    PkgConfig::LIBCARES
    Boost::system
)
