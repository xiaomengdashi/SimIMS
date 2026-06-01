rtp_inc = include_directories('.')

rtp_lib = static_library(
    'imsrtp',
    files(
        'bencode.cpp',
        'rtpengine_client.cpp',
        'media_session.cpp',
    ),
    include_directories: [libinc, rtp_inc],
    dependencies: [core_dep, boost_dep],
)

rtp_dep = declare_dependency(
    link_with: rtp_lib,
    include_directories: [libinc, rtp_inc],
    dependencies: [core_dep, boost_dep],
)
