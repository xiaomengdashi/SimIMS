add_library(ims_media
    bencode.cpp
    rtpengine_client.cpp
    media_session.cpp
)

target_include_directories(ims_media PUBLIC
    ${PROJECT_SOURCE_DIR}/include
PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ims_media PUBLIC
    ims_common
    Boost::system
)
