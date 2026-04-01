add_library(ims_diameter
    aka_vector_builder.cpp
    cx_client.cpp
    rx_client.cpp
)

target_include_directories(ims_diameter PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ims_diameter PUBLIC
    ims_common
    ims_crypt
    ims_sip
    # TODO: add freeDiameter when available
)
