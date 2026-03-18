add_executable(ims_scscf
    main.cpp
    scscf_service.cpp
    registrar.cpp
    auth_manager.cpp
    session_router.cpp
)

target_include_directories(ims_scscf PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ims_scscf PRIVATE
    ims_sip
    ims_dns
    ims_diameter
    ims_registration
    ims_common
)
