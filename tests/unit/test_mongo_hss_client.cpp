#include "diameter/mongo_hss_client.hpp"

#include "common/types.hpp"
#include "mocks/mock_subscriber_repository.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace ims;
using namespace ims::diameter;
using namespace ims::test;
using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrEq;

namespace {

constexpr uint32_t kDiameterSuccess = 2001;
constexpr uint64_t kSqnStep = 32;
constexpr uint64_t kSqnMask = 0x0000FFFFFFFFFFFFULL;

auto make_record() -> db::SubscriberRecord {
    db::SubscriberRecord record;
    record.identities.impi = "001010000000001@ims.mnc001.mcc001.3gppnetwork.org";
    record.identities.canonical_impu = "sip:alice@ims.mnc001.mcc001.3gppnetwork.org";
    record.identities.associated_impus = {
        "sip:alice@ims.mnc001.mcc001.3gppnetwork.org",
        "tel:+8613800000001",
    };
    record.identities.username = "alice";
    record.identities.realm = "ims.mnc001.mcc001.3gppnetwork.org";

    record.auth.ki = "465b5ce8b199b49faa5f0a2ee238a6bc";
    record.auth.operator_code_type = "opc";
    record.auth.opc = "cd63cb71954a9f4e48a5994e37a02baf";
    record.auth.sqn = 0x1234ULL;
    record.auth.amf = "8000";

    record.profile.ifcs = {};
    record.serving.assigned_scscf = "sip:scscf1.ims.mnc001.mcc001.3gppnetwork.org:5062;transport=udp";
    return record;
}

} // namespace

class MongoHssClientTest : public ::testing::Test {
protected:
    HssAdapterConfig config_{};
    MockSubscriberRepository repository_{};

    MongoHssClientTest() {
        config_.default_scscf_uri = "sip:scscf-default.ims.local:5062;transport=udp";
    }
};

TEST_F(MongoHssClientTest, UserAuthorizationSuccess) {
    auto record = make_record();
    EXPECT_CALL(repository_, findByImpiOrImpu(StrEq("001010000000001@ims.mnc001.mcc001.3gppnetwork.org"),
                                              StrEq("sip:alice@ims.mnc001.mcc001.3gppnetwork.org")))
        .WillOnce(Return(Result<std::optional<db::SubscriberRecord>>{record}));

    MongoHssClient client(config_, repository_);
    auto result = client.userAuthorization({
        .impi = "001010000000001@ims.mnc001.mcc001.3gppnetwork.org",
        .impu = "sip:alice@ims.mnc001.mcc001.3gppnetwork.org",
        .visited_network = "ims.mnc001.mcc001.3gppnetwork.org",
    });

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->result_code, kDiameterSuccess);
    EXPECT_EQ(result->assigned_scscf, "sip:scscf1.ims.mnc001.mcc001.3gppnetwork.org:5062;transport=udp");
}

TEST_F(MongoHssClientTest, UserAuthorizationNormalizesImpuBeforeLookup) {
    auto record = make_record();
    EXPECT_CALL(repository_, findByImpiOrImpu(StrEq("001010000000001@ims.mnc001.mcc001.3gppnetwork.org"),
                                              StrEq("sip:+8613800000001@ims.mnc001.mcc001.3gppnetwork.org")))
        .WillOnce(Return(Result<std::optional<db::SubscriberRecord>>{record}));

    MongoHssClient client(config_, repository_);
    auto result = client.userAuthorization({
        .impi = "001010000000001@ims.mnc001.mcc001.3gppnetwork.org",
        .impu = "<sip:+8613800000001@IMS.MNC001.MCC001.3GPPNETWORK.ORG;user=phone>",
        .visited_network = "ims.mnc001.mcc001.3gppnetwork.org",
    });

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->result_code, kDiameterSuccess);
}

TEST_F(MongoHssClientTest, MultimediaAuthSuccessAdvancesSqnAtomically) {
    auto record = make_record();
    EXPECT_CALL(repository_, findByImpiOrImpu(StrEq("001010000000001@ims.mnc001.mcc001.3gppnetwork.org"),
                                              StrEq("sip:alice@ims.mnc001.mcc001.3gppnetwork.org")))
        .WillOnce(Return(Result<std::optional<db::SubscriberRecord>>{record}));

    EXPECT_CALL(repository_,
                advanceSqn(StrEq("001010000000001@ims.mnc001.mcc001.3gppnetwork.org"), kSqnStep, kSqnMask))
        .WillOnce(Return(Result<uint64_t>{record.auth.sqn}));

    MongoHssClient client(config_, repository_);
    auto result = client.multimediaAuth({
        .impi = "001010000000001@ims.mnc001.mcc001.3gppnetwork.org",
        .impu = "sip:alice@ims.mnc001.mcc001.3gppnetwork.org",
        .sip_auth_scheme = "Digest-AKAv1-MD5",
        .server_name = "sip:scscf1.ims.mnc001.mcc001.3gppnetwork.org",
    });

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->result_code, kDiameterSuccess);
    EXPECT_EQ(result->sip_auth_scheme, "Digest-AKAv1-MD5");
    EXPECT_EQ(result->auth_vector.rand.size(), 16u);
    EXPECT_EQ(result->auth_vector.autn.size(), 16u);
    EXPECT_FALSE(result->auth_vector.xres.empty());
}

TEST_F(MongoHssClientTest, MultimediaAuthTwiceCallsAdvanceSqnTwice) {
    auto record = make_record();
    EXPECT_CALL(repository_, findByImpiOrImpu(StrEq("001010000000001@ims.mnc001.mcc001.3gppnetwork.org"),
                                              StrEq("sip:alice@ims.mnc001.mcc001.3gppnetwork.org")))
        .Times(2)
        .WillRepeatedly(Return(Result<std::optional<db::SubscriberRecord>>{record}));

    InSequence sequence;
    EXPECT_CALL(repository_,
                advanceSqn(StrEq("001010000000001@ims.mnc001.mcc001.3gppnetwork.org"), kSqnStep, kSqnMask))
        .WillOnce(Return(Result<uint64_t>{record.auth.sqn}));
    EXPECT_CALL(repository_,
                advanceSqn(StrEq("001010000000001@ims.mnc001.mcc001.3gppnetwork.org"), kSqnStep, kSqnMask))
        .WillOnce(Return(Result<uint64_t>{record.auth.sqn + kSqnStep}));

    MongoHssClient client(config_, repository_);

    auto first = client.multimediaAuth({
        .impi = "001010000000001@ims.mnc001.mcc001.3gppnetwork.org",
        .impu = "sip:alice@ims.mnc001.mcc001.3gppnetwork.org",
        .sip_auth_scheme = "Digest-AKAv1-MD5",
        .server_name = "sip:scscf1.ims.mnc001.mcc001.3gppnetwork.org",
    });
    ASSERT_TRUE(first.has_value()) << first.error().message;

    auto second = client.multimediaAuth({
        .impi = "001010000000001@ims.mnc001.mcc001.3gppnetwork.org",
        .impu = "sip:alice@ims.mnc001.mcc001.3gppnetwork.org",
        .sip_auth_scheme = "Digest-AKAv1-MD5",
        .server_name = "sip:scscf1.ims.mnc001.mcc001.3gppnetwork.org",
    });
    ASSERT_TRUE(second.has_value()) << second.error().message;
}

TEST_F(MongoHssClientTest, ServerAssignmentSuccessUpdatesServingScscf) {
    auto record = make_record();
    EXPECT_CALL(repository_, findByImpiOrImpu(StrEq("001010000000001@ims.mnc001.mcc001.3gppnetwork.org"),
                                              StrEq("sip:alice@ims.mnc001.mcc001.3gppnetwork.org")))
        .WillOnce(Return(Result<std::optional<db::SubscriberRecord>>{record}));
    EXPECT_CALL(repository_, setServingScscf(StrEq("001010000000001@ims.mnc001.mcc001.3gppnetwork.org"),
                                             StrEq("sip:scscf1.ims.mnc001.mcc001.3gppnetwork.org")))
        .WillOnce(Return(VoidResult{}));

    MongoHssClient client(config_, repository_);
    auto result = client.serverAssignment({
        .impi = "001010000000001@ims.mnc001.mcc001.3gppnetwork.org",
        .impu = "sip:alice@ims.mnc001.mcc001.3gppnetwork.org",
        .server_name = "sip:scscf1.ims.mnc001.mcc001.3gppnetwork.org",
        .assignment_type = SarParams::AssignmentType::kRegistration,
    });

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->result_code, kDiameterSuccess);
    EXPECT_EQ(result->user_profile.impu, "sip:alice@ims.mnc001.mcc001.3gppnetwork.org");
    EXPECT_EQ(result->user_profile.associated_impus.size(), 2u);
}

TEST_F(MongoHssClientTest, LocationInfoSuccess) {
    auto record = make_record();
    EXPECT_CALL(repository_, findByIdentity(StrEq("sip:alice@ims.mnc001.mcc001.3gppnetwork.org")))
        .WillOnce(Return(Result<std::optional<db::SubscriberRecord>>{record}));

    MongoHssClient client(config_, repository_);
    auto result = client.locationInfo({
        .impu = "sip:alice@ims.mnc001.mcc001.3gppnetwork.org",
    });

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->result_code, kDiameterSuccess);
    EXPECT_EQ(result->assigned_scscf, "sip:scscf1.ims.mnc001.mcc001.3gppnetwork.org:5062;transport=udp");
}

TEST_F(MongoHssClientTest, LocationInfoNormalizesImpuBeforeLookup) {
    auto record = make_record();
    EXPECT_CALL(repository_, findByIdentity(StrEq("sip:+8613800000001@ims.mnc001.mcc001.3gppnetwork.org")))
        .WillOnce(Return(Result<std::optional<db::SubscriberRecord>>{record}));

    MongoHssClient client(config_, repository_);
    auto result = client.locationInfo({
        .impu = "<sip:+8613800000001@IMS.MNC001.MCC001.3GPPNETWORK.ORG;user=phone>",
    });

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->result_code, kDiameterSuccess);
}

TEST_F(MongoHssClientTest, LocationInfoFallsBackToDefaultScscfWhenServingScscfMissing) {
    auto record = make_record();
    record.serving.assigned_scscf.clear();

    EXPECT_CALL(repository_, findByIdentity(StrEq("sip:alice@ims.mnc001.mcc001.3gppnetwork.org")))
        .WillOnce(Return(Result<std::optional<db::SubscriberRecord>>{record}));

    MongoHssClient client(config_, repository_);
    auto result = client.locationInfo({
        .impu = "sip:alice@ims.mnc001.mcc001.3gppnetwork.org",
    });

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->result_code, kDiameterSuccess);
    EXPECT_EQ(result->assigned_scscf, config_.default_scscf_uri);
}

TEST_F(MongoHssClientTest, UserAuthorizationReturnsUserNotFoundWhenSubscriberMissing) {
    EXPECT_CALL(repository_, findByImpiOrImpu(StrEq("unknown@ims.mnc001.mcc001.3gppnetwork.org"),
                                              StrEq("sip:unknown@ims.mnc001.mcc001.3gppnetwork.org")))
        .WillOnce(Return(Result<std::optional<db::SubscriberRecord>>{std::nullopt}));

    MongoHssClient client(config_, repository_);
    auto result = client.userAuthorization({
        .impi = "unknown@ims.mnc001.mcc001.3gppnetwork.org",
        .impu = "sip:unknown@ims.mnc001.mcc001.3gppnetwork.org",
        .visited_network = "ims.mnc001.mcc001.3gppnetwork.org",
    });

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kDiameterUserNotFound);
}
