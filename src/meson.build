srcinc = include_directories('.')

scscf_sources = files(
    's-cscf/scscf_service.cpp',
    's-cscf/registrar.cpp',
    's-cscf/auth_manager.cpp',
    's-cscf/digest_credential_store.cpp',
    's-cscf/mongo_digest_credential_store.cpp',
    's-cscf/digest_auth_provider.cpp',
    's-cscf/ims_aka_auth_provider.cpp',
    's-cscf/session_router.cpp',
)

pcscf_sources = files(
    'p-cscf/pcscf_service.cpp',
)

icscf_sources = files(
    'i-cscf/icscf_service.cpp',
    'i-cscf/scscf_selector.cpp',
)

ims_pcscf = executable(
    'ims_pcscf',
    files('p-cscf/main.cpp') + pcscf_sources,
    include_directories: [srcinc, libinc],
    dependencies: [sip_dep, dns_dep, diameter_dep, rtp_dep, core_dep, sms_dep],
    build_rpath: '$ORIGIN',
    install_rpath: '$ORIGIN',
    install: true,
)

ims_icscf = executable(
    'ims_icscf',
    files('i-cscf/main.cpp') + icscf_sources,
    include_directories: [srcinc, libinc],
    dependencies: [sip_dep, dns_dep, diameter_dep, core_dep, sms_dep],
    build_rpath: '$ORIGIN',
    install_rpath: '$ORIGIN',
    install: true,
)

ims_scscf = executable(
    'ims_scscf',
    files('s-cscf/main.cpp') + scscf_sources,
    include_directories: [srcinc, libinc],
    dependencies: [diameter_dep, dns_dep, db_dep, sms_dep],
    build_rpath: '$ORIGIN',
    install_rpath: '$ORIGIN',
    install: true,
)

ims_smsc = executable(
    'ims_smsc',
    files('smsc/main.cpp'),
    include_directories: [srcinc, libinc],
    dependencies: [sms_dep, core_dep],
    build_rpath: '$ORIGIN',
    install_rpath: '$ORIGIN',
    install: true,
)

ims_allinone = executable(
    'ims_allinone',
    files('allinone/main.cpp') + pcscf_sources + icscf_sources + scscf_sources,
    include_directories: [srcinc, libinc],
    dependencies: [sip_dep, dns_dep, diameter_dep, db_dep, rtp_dep, core_dep, sms_dep],
    build_rpath: '$ORIGIN',
    install_rpath: '$ORIGIN',
    install: true,
)

if get_option('link_bin')
    bin_dir = meson.project_source_root() / 'bin'
    run_command('mkdir', '-p', bin_dir, check: false)
    ims_bins = [
        ['ims_pcscf', ims_pcscf],
        ['ims_icscf', ims_icscf],
        ['ims_scscf', ims_scscf],
        ['ims_smsc', ims_smsc],
        ['ims_allinone', ims_allinone],
    ]
    foreach b : ims_bins
        custom_target(
            'link_' + b[0],
            output: 'symlink_' + b[0],
            command: [
                'ln',
                '-sf',
                b[1].full_path(),
                bin_dir / b[0],
            ],
            depends: b[1],
            build_by_default: true,
        )
    endforeach
endif
