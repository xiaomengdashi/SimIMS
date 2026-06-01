core_inc = include_directories('.')

core_lib = static_library(
    'imscore',
    files(
        'config.cpp',
        'logger.cpp',
        'io_context.cpp',
    ),
    include_directories: [libinc, core_inc],
    dependencies: [spdlog_dep, yaml_cpp_dep, boost_dep],
)

core_dep = declare_dependency(
    link_with: core_lib,
    include_directories: [libinc, core_inc],
    dependencies: [spdlog_dep, yaml_cpp_dep, boost_dep],
)
