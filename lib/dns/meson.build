dns_inc = include_directories('.')

dns_lib = static_library(
    'imsdns',
    files('resolver.cpp'),
    include_directories: [libinc, dns_inc],
    dependencies: [core_dep, libcares_dep],
)

dns_dep = declare_dependency(
    link_with: dns_lib,
    include_directories: [libinc, dns_inc],
    dependencies: [core_dep, libcares_dep],
)
