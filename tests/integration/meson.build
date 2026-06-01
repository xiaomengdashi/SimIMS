integration_inc = include_directories(
    '../../tests',
    '../..',
    '../../src',
    '../../lib',
)

clang_cpp_args = []
if cpp.get_id() == 'clang'
    clang_cpp_args += ['-Wno-character-conversion']
endif

integration_sources = files(
    'test_full_flow.cpp',
    '../../src/p-cscf/pcscf_service.cpp',
    '../../src/i-cscf/icscf_service.cpp',
    '../../src/i-cscf/scscf_selector.cpp',
    '../../src/s-cscf/scscf_service.cpp',
    '../../src/s-cscf/registrar.cpp',
    '../../src/s-cscf/auth_manager.cpp',
    '../../src/s-cscf/digest_credential_store.cpp',
    '../../src/s-cscf/digest_auth_provider.cpp',
    '../../src/s-cscf/ims_aka_auth_provider.cpp',
    '../../src/s-cscf/session_router.cpp',
)

test_full_flow = executable(
    'test_full_flow',
    integration_sources,
    cpp_args: clang_cpp_args,
    include_directories: [integration_inc, libinc, srcinc],
    dependencies: [sip_dep, sms_dep, diameter_dep, rtp_dep, core_dep, gtest_dep, gmock_dep],
)

test(
    'test_full_flow',
    test_full_flow,
    suite: ['integration', 'e2e'],
)

bash = find_program('bash', required: false)
if bash.found()
    test(
        'BaresipInviteIntegrationTest',
        bash,
        args: [meson.project_source_root() / 'tools/test_baresip_invite.sh'],
        suite: 'integration',
        protocol: 'exitcode',
        is_parallel: false,
    )
endif
