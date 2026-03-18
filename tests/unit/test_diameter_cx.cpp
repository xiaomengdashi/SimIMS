#include "ims/diameter/ihss_client.hpp"
#include "ims/diameter/types.hpp"
#include "../mocks/mock_hss_client.hpp"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace ims;
using namespace ims::diameter;
using namespace ims::test;
using ::testing::_;
using ::testing::Return;

class DiameterCxTest : public ::testing::Test {
protected:
    MockHssClient hss_;
};

TEST_F(DiameterCxTest, UserAuthorizationSuccess) {
    UarParams params{
        .impi = "user@ims.example.com",
        .impu = "sip:user@ims.example.com",
        .visited_network = "ims.example.com",
        .auth_type = UarParams::AuthType::kRegistration,
    };

    UaaResult expected{
        .result_code = 2001,  // DIAMETER_SUCCESS
        .assigned_scscf = "sip:scscf.ims.example.com:5062",
    };

    EXPECT_CALL(hss_, userAuthorization(_))
        .WillOnce(Return(Result<UaaResult>{expected}));

    auto result = hss_.userAuthorization(params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->result_code, 2001u);
    EXPECT_EQ(result->assigned_scscf, "sip:scscf.ims.example.com:5062");
}

TEST_F(DiameterCxTest, MultimediaAuthReturnsVector) {
    MarParams params{
        .impi = "user@ims.example.com",
        .impu = "sip:user@ims.example.com",
        .sip_auth_scheme = "Digest-AKAv1-MD5",
        .server_name = "sip:scscf.ims.example.com",
    };

    MaaResult expected{
        .result_code = 2001,
        .sip_auth_scheme = "Digest-AKAv1-MD5",
        .auth_vector = {
            .rand = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                     0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10},
            .autn = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                     0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20},
            .xres = {0xaa, 0xbb, 0xcc, 0xdd},
            .ck = std::vector<uint8_t>(16, 0x55),
            .ik = std::vector<uint8_t>(16, 0x66),
        },
    };

    EXPECT_CALL(hss_, multimediaAuth(_))
        .WillOnce(Return(Result<MaaResult>{expected}));

    auto result = hss_.multimediaAuth(params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->auth_vector.rand.size(), 16u);
    EXPECT_EQ(result->auth_vector.xres.size(), 4u);
}

TEST_F(DiameterCxTest, ServerAssignmentDownloadsProfile) {
    SarParams params{
        .impi = "user@ims.example.com",
        .impu = "sip:user@ims.example.com",
        .server_name = "sip:scscf.ims.example.com",
        .assignment_type = SarParams::AssignmentType::kRegistration,
    };

    SaaResult expected{
        .result_code = 2001,
        .user_profile = {
            .impu = "sip:user@ims.example.com",
            .associated_impus = {"sip:user@ims.example.com", "tel:+1234567890"},
            .ifcs = {},
        },
    };

    EXPECT_CALL(hss_, serverAssignment(_))
        .WillOnce(Return(Result<SaaResult>{expected}));

    auto result = hss_.serverAssignment(params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->user_profile.impu, "sip:user@ims.example.com");
    EXPECT_EQ(result->user_profile.associated_impus.size(), 2u);
}

TEST_F(DiameterCxTest, LocationInfoForMtRouting) {
    LirParams params{
        .impu = "sip:callee@ims.example.com",
    };

    LiaResult expected{
        .result_code = 2001,
        .assigned_scscf = "sip:scscf.ims.example.com:5062",
    };

    EXPECT_CALL(hss_, locationInfo(_))
        .WillOnce(Return(Result<LiaResult>{expected}));

    auto result = hss_.locationInfo(params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->assigned_scscf, "sip:scscf.ims.example.com:5062");
}

TEST_F(DiameterCxTest, UserNotFound) {
    LirParams params{
        .impu = "sip:unknown@ims.example.com",
    };

    EXPECT_CALL(hss_, locationInfo(_))
        .WillOnce(Return(Result<LiaResult>{
            std::unexpected(ErrorInfo{ErrorCode::kDiameterUserNotFound, "User not found"})}));

    auto result = hss_.locationInfo(params);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kDiameterUserNotFound);
}
