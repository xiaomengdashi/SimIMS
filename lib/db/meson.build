db_inc = include_directories('.')

db_lib = static_library(
    'imsdb',
    files(
        'mongo_client.cpp',
        'subscriber_codec.cpp',
        'mongo_subscriber_repository.cpp',
    ),
    include_directories: [libinc, db_inc],
    dependencies: [core_dep, sip_dep, mongoc_dep, bson_dep],
)

db_dep = declare_dependency(
    link_with: db_lib,
    include_directories: [libinc, db_inc],
    dependencies: [core_dep, sip_dep, mongoc_dep, bson_dep],
)
