add_executable(ims_scscf
    main.cpp
    scscf_service.cpp
    registrar.cpp
    auth_manager.cpp
    digest_credential_store.cpp
    mongo_digest_credential_store.cpp
    digest_auth_provider.cpp
    ims_aka_auth_provider.cpp
    session_router.cpp
)

target_include_directories(ims_scscf PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ims_scscf PRIVATE
    ims_diameter
    ims_dns
    ims_db
)
