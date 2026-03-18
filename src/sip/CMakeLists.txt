add_library(ims_sip
    message.cpp
    stack.cpp
    transaction.cpp
    transport.cpp
    dialog.cpp
    proxy_core.cpp
)

target_include_directories(ims_sip PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(ims_sip PUBLIC
    ims_common
    PkgConfig::LIBOSIP2
    Boost::system
)
