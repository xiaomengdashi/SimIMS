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
    MOCK_METHOD(Result<bool>, upsertContact,
                (std::string_view,
                 const registration::ContactBindingSelector&,
                 const registration::ContactBinding&,
                 std::string_view,
                 std::string_view,
                 registration::RegistrationBinding::State,
                 bool,
                 bool),
                (override));
    MOCK_METHOD(Result<bool>, removeContact,
                (std::string_view,
                 const registration::ContactBindingSelector&),
                (override));
    MOCK_METHOD(VoidResult, remove,
                (std::string_view), (override));
    MOCK_METHOD(Result<size_t>, purgeExpired, (), (override));
    MOCK_METHOD(Result<bool>, isRegistered,
                (std::string_view), (override));
};

} // namespace ims::test
