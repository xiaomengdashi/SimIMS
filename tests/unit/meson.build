test_inc = include_directories(
    '../../tests',
    '../..',
    '../../src',
    '../../lib',
)

clang_cpp_args = []
if cpp.get_id() == 'clang'
    clang_cpp_args += ['-Wno-character-conversion']
endif

scscf_auth_sources = [
    '../../src/s-cscf/auth_manager.cpp',
    '../../src/s-cscf/digest_credential_store.cpp',
    '../../src/s-cscf/digest_auth_provider.cpp',
    '../../src/s-cscf/registrar.cpp',
]

scscf_full_sources = [
    '../../src/s-cscf/scscf_service.cpp',
    '../../src/s-cscf/registrar.cpp',
    '../../src/s-cscf/auth_manager.cpp',
    '../../src/s-cscf/digest_credential_store.cpp',
    '../../src/s-cscf/mongo_digest_credential_store.cpp',
    '../../src/s-cscf/digest_auth_provider.cpp',
    '../../src/s-cscf/ims_aka_auth_provider.cpp',
    '../../src/s-cscf/session_router.cpp',
]

unit_tests = [
    {
        'name': 'test_sip_message',
        'sources': ['test_sip_message.cpp'],
        'deps': [sip_dep],
    },
    {
        'name': 'test_proxy_core',
        'sources': ['test_proxy_core.cpp'],
        'deps': [sip_dep, core_dep],
    },
    {
        'name': 'test_transaction_layer',
        'sources': ['test_transaction_layer.cpp'],
        'deps': [sip_dep, core_dep],
    },
    {
        'name': 'test_sip_stack',
        'sources': ['test_sip_stack.cpp'],
        'deps': [sip_dep, core_dep],
    },
    {
        'name': 'test_transport',
        'sources': ['test_transport.cpp'],
        'deps': [sip_dep],
    },
    {
        'name': 'test_dns_resolver',
        'sources': ['test_dns_resolver.cpp'],
        'deps': [dns_dep],
    },
    {
        'name': 'test_bencode',
        'sources': ['test_bencode.cpp'],
        'deps': [rtp_dep],
    },
    {
        'name': 'test_rtpengine_client',
        'sources': ['test_rtpengine_client.cpp'],
        'deps': [rtp_dep],
    },
    {
        'name': 'test_media_session',
        'sources': ['test_media_session.cpp'],
        'deps': [rtp_dep, core_dep],
    },
    {
        'name': 'test_milenage',
        'sources': ['test_milenage.cpp'],
        'deps': [crypt_dep, core_dep],
    },
    {
        'name': 'test_config',
        'sources': ['test_config.cpp'],
        'deps': [core_dep],
    },
    {
        'name': 'test_diameter_cx',
        'sources': ['test_diameter_cx.cpp'],
        'deps': [diameter_dep, core_dep],
    },
    {
        'name': 'test_registrar',
        'sources': ['test_registrar.cpp'] + scscf_auth_sources,
        'deps': [core_dep, sip_dep, diameter_dep],
    },
    {
        'name': 'test_registration_store',
        'sources': ['test_registration_store.cpp'],
        'deps': [sip_dep, core_dep],
    },
    {
        'name': 'test_auth_manager',
        'sources': ['test_auth_manager.cpp', '../../src/s-cscf/auth_manager.cpp'],
        'deps': [sip_dep, core_dep],
    },
    {
        'name': 'test_auth_providers',
        'sources': [
            'test_auth_providers.cpp',
            '../../src/s-cscf/auth_manager.cpp',
            '../../src/s-cscf/digest_credential_store.cpp',
            '../../src/s-cscf/mongo_digest_credential_store.cpp',
            '../../src/s-cscf/digest_auth_provider.cpp',
            '../../src/s-cscf/ims_aka_auth_provider.cpp',
        ],
        'deps': [sip_dep, core_dep, diameter_dep, db_dep],
    },
    {
        'name': 'test_session_router',
        'sources': ['test_session_router.cpp', '../../src/s-cscf/session_router.cpp'],
        'deps': [sip_dep, sms_dep, core_dep],
    },
    {
        'name': 'test_sms_over_ims',
        'sources': ['test_sms_over_ims.cpp'],
        'deps': [sms_dep],
    },
    {
        'name': 'test_scscf_service',
        'sources': ['test_scscf_service.cpp'] + scscf_full_sources,
        'deps': [dns_dep, diameter_dep, db_dep, sms_dep],
    },
    {
        'name': 'test_pcscf_service',
        'sources': ['test_pcscf_service.cpp', '../../src/p-cscf/pcscf_service.cpp'],
        'deps': [sip_dep, sms_dep, diameter_dep, rtp_dep, core_dep],
    },
    {
        'name': 'test_icscf_service',
        'sources': [
            'test_icscf_service.cpp',
            '../../src/i-cscf/icscf_service.cpp',
            '../../src/i-cscf/scscf_selector.cpp',
        ],
        'deps': [sip_dep, sms_dep, diameter_dep, core_dep],
    },
    {
        'name': 'test_subscriber_codec',
        'sources': ['test_subscriber_codec.cpp'],
        'deps': [db_dep, core_dep],
    },
    {
        'name': 'test_mongo_hss_client',
        'sources': ['test_mongo_hss_client.cpp'],
        'deps': [diameter_dep, core_dep],
    },
    {
        'name': 'test_mongo_digest_credential_store',
        'sources': [
            'test_mongo_digest_credential_store.cpp',
            '../../src/s-cscf/mongo_digest_credential_store.cpp',
        ],
        'deps': [core_dep, db_dep, sip_dep],
    },
    {
        'name': 'test_mongo_subscriber_repository',
        'sources': ['test_mongo_subscriber_repository.cpp'],
        'deps': [db_dep, core_dep],
    },
]

foreach t : unit_tests
    exe = executable(
        t['name'],
        files(t['sources']),
        cpp_args: clang_cpp_args,
        include_directories: [test_inc, libinc, srcinc],
        dependencies: t['deps'] + [gtest_dep, gmock_dep],
    )
    test(t['name'], exe, suite: 'unit')
endforeach
