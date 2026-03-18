add_library(ims_registration
    memory_store.cpp
)

target_include_directories(ims_registration PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ims_registration PUBLIC
    ims_common
)
