#include "sip/store.hpp"
#include "sip/uri_utils.hpp"
#include "../mocks/mock_hss_client.hpp"
#include "../mocks/mock_registration_store.hpp"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace ims;
using namespace ims::test;
using namespace ims::registration;
using ::testing::_;
using ::testing::Return;

class RegistrationStoreTest : public ::testing::Test {
protected:
    MockRegistrationStore store_;
};

TEST_F(RegistrationStoreTest, StoreAndLookup) {
    RegistrationBinding binding;
    binding.impu = "sip:user@ims.example.com";
    binding.impi = "user@ims.example.com";
    binding.state = RegistrationBinding::State::kRegistered;
    binding.contacts.push_back({
        .contact_uri = "sip:user@10.0.0.1:5060",
        .expires = std::chrono::steady_clock::now() + std::chrono::hours(1),
    });

    EXPECT_CALL(store_, store(_))
        .WillOnce(Return(VoidResult{}));
    EXPECT_CALL(store_, lookup("sip:user@ims.example.com"))
        .WillOnce(Return(Result<RegistrationBinding>{binding}));

    auto store_result = store_.store(binding);
    ASSERT_TRUE(store_result.has_value());

    auto lookup_result = store_.lookup("sip:user@ims.example.com");
    ASSERT_TRUE(lookup_result.has_value());
    EXPECT_EQ(lookup_result->impu, "sip:user@ims.example.com");
    EXPECT_EQ(lookup_result->contacts.size(), 1u);
}

TEST_F(RegistrationStoreTest, LookupNotFound) {
    EXPECT_CALL(store_, lookup("sip:unknown@ims.example.com"))
        .WillOnce(Return(Result<RegistrationBinding>{
            std::unexpected(ErrorInfo{ErrorCode::kRegistrationNotFound, "Not found"})}));

    auto result = store_.lookup("sip:unknown@ims.example.com");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kRegistrationNotFound);
}

TEST_F(RegistrationStoreTest, Remove) {
    EXPECT_CALL(store_, remove("sip:user@ims.example.com"))
        .WillOnce(Return(VoidResult{}));

    auto result = store_.remove("sip:user@ims.example.com");
    ASSERT_TRUE(result.has_value());
}

TEST_F(RegistrationStoreTest, IsRegistered) {
    EXPECT_CALL(store_, isRegistered("sip:user@ims.example.com"))
        .WillOnce(Return(Result<bool>{true}));
    EXPECT_CALL(store_, isRegistered("sip:unknown@ims.example.com"))
        .WillOnce(Return(Result<bool>{false}));

    auto result1 = store_.isRegistered("sip:user@ims.example.com");
    ASSERT_TRUE(result1.has_value());
    EXPECT_TRUE(*result1);

    auto result2 = store_.isRegistered("sip:unknown@ims.example.com");
    ASSERT_TRUE(result2.has_value());
    EXPECT_FALSE(*result2);
}

// Test the ActiveContacts helper on RegistrationBinding
TEST(RegistrationBindingTest, ActiveContactsFiltersExpired) {
    RegistrationBinding binding;
    binding.impu = "sip:user@ims.example.com";

    auto now = std::chrono::steady_clock::now();

    // Active contact
    binding.contacts.push_back({
        .contact_uri = "sip:active@10.0.0.1",
        .expires = now + std::chrono::hours(1),
    });

    // Expired contact
    binding.contacts.push_back({
        .contact_uri = "sip:expired@10.0.0.2",
        .expires = now - std::chrono::hours(1),
    });

    auto active = binding.active_contacts();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0]->contact_uri, "sip:active@10.0.0.1");
}

TEST(UriUtilsTest, NormalizeImpuUriSupportsTelAndSipAliases) {
    EXPECT_EQ(ims::sip::normalize_impu_uri("tel:+8613824122023;phone-context=ims.operator.com"),
              "tel:+8613824122023");
    EXPECT_EQ(ims::sip::normalize_impu_uri("<sip:+8613824122023@IMS.OPERATOR.COM;user=phone>"),
              "sip:+8613824122023@ims.operator.com");
    EXPECT_EQ(ims::sip::normalize_impu_uri("sip:460112024122023@IMS.OPERATOR.COM"),
              "sip:460112024122023@ims.operator.com");
}
