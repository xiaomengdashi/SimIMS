#pragma once

#include "diameter/ihss_client.hpp"
#include <gmock/gmock.h>

namespace ims::test {

class MockHssClient : public diameter::IHssClient {
public:
    MOCK_METHOD(Result<diameter::UaaResult>, userAuthorization,
                (const diameter::UarParams&), (override));
    MOCK_METHOD(Result<diameter::MaaResult>, multimediaAuth,
                (const diameter::MarParams&), (override));
    MOCK_METHOD(Result<diameter::SaaResult>, serverAssignment,
                (const diameter::SarParams&), (override));
    MOCK_METHOD(Result<diameter::LiaResult>, locationInfo,
                (const diameter::LirParams&), (override));
};

} // namespace ims::test
