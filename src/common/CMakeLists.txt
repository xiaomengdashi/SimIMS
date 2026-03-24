add_library(ims_common
    config.cpp
    logger.cpp
    io_context.cpp
)

target_include_directories(ims_common PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ims_common PUBLIC
    spdlog::spdlog
    yaml-cpp::yaml-cpp
    Boost::system
)
