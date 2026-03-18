add_executable(ims_pcscf
    main.cpp
    pcscf_service.cpp
)

target_include_directories(ims_pcscf PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ims_pcscf PRIVATE
    ims_sip
    ims_dns
    ims_diameter
    ims_media
    ims_common
)
