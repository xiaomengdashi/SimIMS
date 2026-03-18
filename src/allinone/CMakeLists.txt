add_executable(ims_allinone
    main.cpp
)

target_include_directories(ims_allinone PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ims_allinone PRIVATE
    ims_sip
    ims_dns
    ims_diameter
    ims_media
    ims_registration
    ims_common
)

# All-in-one needs access to all CSCF service sources
target_sources(ims_allinone PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pcscf/pcscf_service.cpp
    ${CMAKE_SOURCE_DIR}/src/icscf/icscf_service.cpp
    ${CMAKE_SOURCE_DIR}/src/icscf/scscf_selector.cpp
    ${CMAKE_SOURCE_DIR}/src/scscf/scscf_service.cpp
    ${CMAKE_SOURCE_DIR}/src/scscf/registrar.cpp
    ${CMAKE_SOURCE_DIR}/src/scscf/auth_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/scscf/session_router.cpp
)
