sms_inc = include_directories('.')

sms_lib = static_library(
    'imssms',
    files(
        'address.cpp',
        'rp_message.cpp',
        'tpdu.cpp',
        'user_data.cpp',
        'hex_utils.cpp',
        'sms_validator.cpp',
        'smsc_service.cpp',
    ),
    include_directories: [libinc, sms_inc],
    dependencies: [core_dep, sip_dep],
)

sms_dep = declare_dependency(
    link_with: sms_lib,
    include_directories: [libinc, sms_inc],
    dependencies: [core_dep, sip_dep],
)
