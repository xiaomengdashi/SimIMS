#pragma once

#include "diameter/ipcf_client.hpp"
#include <gmock/gmock.h>

namespace ims::test {

class MockPcfClient : public diameter::IPcfClient {
public:
    MOCK_METHOD(Result<diameter::AaaResult>, authorizeSession,
                (const diameter::AarParams&), (override));
    MOCK_METHOD(Result<diameter::StaResult>, terminateSession,
                (const diameter::StrParams&), (override));
    MOCK_METHOD(void, setAsrHandler,
                (AsrHandler), (override));
};

} // namespace ims::test
