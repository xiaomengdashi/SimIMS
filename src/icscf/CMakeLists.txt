add_executable(ims_icscf
    main.cpp
    icscf_service.cpp
    scscf_selector.cpp
)

target_include_directories(ims_icscf PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ims_icscf PRIVATE
    ims_sip
    ims_dns
    ims_diameter
    ims_common
)
