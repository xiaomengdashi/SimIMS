#pragma once

#include "ims/media/rtpengine_client.hpp"
#include <gmock/gmock.h>

namespace ims::test {

class MockRtpengineClient : public media::IRtpengineClient {
public:
    MOCK_METHOD(Result<media::RtpengineResult>, offer,
                (const media::MediaSession&, const std::string&, const media::RtpengineFlags&),
                (override));
    MOCK_METHOD(Result<media::RtpengineResult>, answer,
                (const media::MediaSession&, const std::string&, const media::RtpengineFlags&),
                (override));
    MOCK_METHOD(VoidResult, deleteSession,
                (const media::MediaSession&), (override));
    MOCK_METHOD(Result<std::string>, query,
                (const media::MediaSession&), (override));
    MOCK_METHOD(VoidResult, ping, (), (override));
};

} // namespace ims::test
