sip_inc = include_directories('.')

sip_lib = static_library(
    'imssip',
    files(
        'message.cpp',
        'uri_utils.cpp',
        'stack.cpp',
        'transaction.cpp',
        'transport.cpp',
        'proxy_core.cpp',
        'memory_store.cpp',
        'reg_event_notifier.cpp',
    ),
    include_directories: [libinc, sip_inc],
    dependencies: [core_dep, libosip2_dep],
)

sip_dep = declare_dependency(
    link_with: sip_lib,
    include_directories: [libinc, sip_inc],
    dependencies: [core_dep, libosip2_dep],
)
