#pragma once

#include "sip/transport.hpp"
#include <gmock/gmock.h>

namespace ims::test {

class MockTransport : public sip::ITransport {
public:
    MOCK_METHOD(VoidResult, send,
                (const sip::SipMessage&, const sip::Endpoint&), (override));
    MOCK_METHOD(void, setMessageCallback,
                (sip::MessageCallback), (override));
    MOCK_METHOD(VoidResult, start, (), (override));
    MOCK_METHOD(void, stop, (), (override));
    MOCK_METHOD(sip::Endpoint, localEndpoint, (), (const, override));
};

} // namespace ims::test
