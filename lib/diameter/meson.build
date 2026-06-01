diameter_inc = include_directories('.')

diameter_lib = static_library(
    'imsdiameter',
    files(
        'aka_vector_builder.cpp',
        'cx_client.cpp',
        'mongo_hss_client.cpp',
        'rx_client.cpp',
    ),
    include_directories: [libinc, diameter_inc],
    dependencies: [core_dep, crypt_dep, sip_dep, db_dep],
)

diameter_dep = declare_dependency(
    link_with: diameter_lib,
    include_directories: [libinc, diameter_inc],
    dependencies: [core_dep, crypt_dep, sip_dep, db_dep],
)
