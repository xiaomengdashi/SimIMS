#pragma once

#include "db/subscriber_repository.hpp"

#include <gmock/gmock.h>

namespace ims::test {

class MockSubscriberRepository : public db::ISubscriberRepository {
public:
    MOCK_METHOD((Result<std::optional<db::SubscriberRecord>>),
                findByImpiOrImpu,
                (std::string_view impi, std::string_view impu),
                (const, override));

    MOCK_METHOD((Result<std::optional<db::SubscriberRecord>>),
                findByIdentity,
                (std::string_view identity),
                (const, override));

    MOCK_METHOD((Result<std::optional<db::SubscriberRecord>>),
                findByUsernameRealm,
                (std::string_view username, std::string_view realm),
                (const, override));

    MOCK_METHOD(VoidResult, setSqn, (std::string_view impi, uint64_t sqn), (override));

    MOCK_METHOD(VoidResult,
                incrementSqn,
                (std::string_view impi, uint64_t step, uint64_t mask),
                (override));

    MOCK_METHOD((Result<uint64_t>),
                advanceSqn,
                (std::string_view impi, uint64_t step, uint64_t mask),
                (override));

    MOCK_METHOD(VoidResult,
                setServingScscf,
                (std::string_view impi, std::string_view scscf_uri),
                (override));
};

} // namespace ims::test
