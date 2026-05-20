add_executable(ims_smsc
    main.cpp
)

target_include_directories(ims_smsc PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ims_smsc PRIVATE
    ims_sms
    ims_common
)
