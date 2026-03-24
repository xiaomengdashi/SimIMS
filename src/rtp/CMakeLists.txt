add_library(ims_rtp
    bencode.cpp
    rtpengine_client.cpp
    media_session.cpp
)

target_include_directories(ims_rtp PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ims_rtp PUBLIC
    ims_common
    Boost::system
)
