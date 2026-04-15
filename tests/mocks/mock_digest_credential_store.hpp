#pragma once

#include "s-cscf/digest_credential_store.hpp"

#include <gmock/gmock.h>

namespace ims::test {

class MockDigestCredentialStore : public scscf::IDigestCredentialStore {
public:
    MOCK_METHOD((Result<std::optional<scscf::DigestCredential>>),
                findByUsername,
                (std::string_view username, std::string_view realm),
                (const, override));

    MOCK_METHOD((Result<std::optional<scscf::DigestCredential>>),
                findByIdentity,
                (std::string_view identity),
                (const, override));
};

} // namespace ims::test
