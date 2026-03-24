#pragma once

#include "sip/store.hpp"
#include <gmock/gmock.h>

namespace ims::test {

class MockRegistrationStore : public registration::IRegistrationStore {
public:
    MOCK_METHOD(VoidResult, store,
                (const registration::RegistrationBinding&), (override));
    MOCK_METHOD(Result<registration::RegistrationBinding>, lookup,
                (std::string_view), (override));
    MOCK_METHOD(VoidResult, remove,
                (std::string_view), (override));
    MOCK_METHOD(Result<size_t>, purgeExpired, (), (override));
    MOCK_METHOD(Result<bool>, isRegistered,
                (std::string_view), (override));
};

} // namespace ims::test
