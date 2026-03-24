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
    ims_rtp
    ims_common
)

# All-in-one needs access to all CSCF service sources
target_sources(ims_allinone PRIVATE
    ${CMAKE_SOURCE_DIR}/src/p-cscf/pcscf_service.cpp
    ${CMAKE_SOURCE_DIR}/src/i-cscf/icscf_service.cpp
    ${CMAKE_SOURCE_DIR}/src/i-cscf/scscf_selector.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/scscf_service.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/registrar.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/auth_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/s-cscf/session_router.cpp
)
