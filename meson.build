project(
    'ims',
    'cpp',
    version: '0.1.0',
    meson_version: '>= 1.0.0',
    default_options: [
        'cpp_std=c++23',
        'warning_level=2',
        'werror=false',
    ],
)

cpp = meson.get_compiler('cpp')

# External dependencies
boost_dep = dependency('boost', modules: ['system'])
spdlog_dep = dependency('spdlog')
yaml_cpp_dep = dependency('yaml-cpp')
gtest_dep = dependency('gtest', main: true)
gmock_dep = dependency('gmock')
libosip2_dep = dependency('libosip2')
libcares_dep = dependency('libcares')

mongoc_dep = dependency('libmongoc-1.0', required: false)
if not mongoc_dep.found()
    mongoc_dep = dependency('mongoc2')
endif

bson_dep = dependency('libbson-1.0', required: false)
if not bson_dep.found()
    bson_dep = dependency('bson2')
endif

subdir('lib')
subdir('src')

build_tests = get_option('tests')
if build_tests
    subdir('tests')
endif

summary({
    'prefix': get_option('prefix'),
    'source': meson.project_source_root(),
    'build': meson.project_build_root(),
}, section: 'IMS build')
