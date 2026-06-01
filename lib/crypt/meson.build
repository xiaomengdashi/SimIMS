crypt_inc = include_directories('.')

crypt_lib = static_library(
    'imscrypt',
    files(
        'milenage.cpp',
        'milenage_adapter.cpp',
    ),
    include_directories: [libinc, crypt_inc],
    dependencies: [core_dep],
)

crypt_dep = declare_dependency(
    link_with: crypt_lib,
    include_directories: [libinc, crypt_inc],
    dependencies: [core_dep],
)
