add_library(ims_diameter
    cx_client.cpp
    rx_client.cpp
)

target_include_directories(ims_diameter PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ims_diameter PUBLIC
    ims_common
    # TODO: add freeDiameter when available
)
