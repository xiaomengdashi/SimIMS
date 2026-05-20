add_library(ims_sms
    address.cpp
    rp_message.cpp
    tpdu.cpp
    user_data.cpp
    hex_utils.cpp
    sms_validator.cpp
    smsc_service.cpp
)

target_include_directories(ims_sms PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ims_sms PUBLIC
    ims_common
    ims_sip
)

target_compile_features(ims_sms PUBLIC cxx_std_23)
