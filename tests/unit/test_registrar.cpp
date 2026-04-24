#include "sip/store.hpp"
#include "sip/memory_store.hpp"
#include "sip/uri_utils.hpp"
#include "s-cscf/auth_manager.hpp"
#include "s-cscf/digest_auth_provider.hpp"
#include "s-cscf/digest_credential_store.hpp"
#include "s-cscf/registrar.hpp"
#include "../mocks/mock_hss_client.hpp"
#include "../mocks/mock_registration_store.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/uuid/detail/md5.hpp>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <iomanip>
#include <sstream>

using namespace ims;
using namespace ims::test;
using namespace ims::registration;
using ::testing::_;
using ::testing::Return;

namespace {

class CapturingTransport final : public ims::sip::ITransport {
public:
    auto send(const ims::sip::SipMessage& msg, const ims::sip::Endpoint& dest) -> ims::VoidResult override {
        auto clone = msg.clone();
        if (!clone) {
            return std::unexpected(clone.error());
        }
        sent_messages.push_back(std::move(*clone));
        sent_destinations.push_back(dest);
        return {};
    }

    void setMessageCallback(MessageCallback cb) override {
        callback = std::move(cb);
    }

    auto start() -> ims::VoidResult override { return {}; }
    void stop() override {}

    auto localEndpoint() const -> ims::sip::Endpoint override {
        return ims::sip::Endpoint{.address = "127.0.0.1", .port = 5062, .transport = "udp"};
    }

    MessageCallback callback;
    std::vector<ims::sip::SipMessage> sent_messages;
    std::vector<ims::sip::Endpoint> sent_destinations;
};

auto md5Hex(const std::string& input) -> std::string {
    boost::uuids::detail::md5 hash;
    boost::uuids::detail::md5::digest_type digest{};

    hash.process_bytes(input.data(), input.size());
    hash.get_digest(digest);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto word : digest) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            oss << std::setw(2) << ((word >> shift) & 0xFF);
        }
    }
    return oss.str();
}

auto computeDigestResponse(const std::string& username,
                           const std::string& realm,
                           const std::string& password,
                           const std::string& method,
                           const std::string& uri,
                           const std::string& nonce,
                           const std::string& nc,
                           const std::string& cnonce,
                           const std::string& qop) -> std::string {
    auto ha1 = md5Hex(username + ":" + realm + ":" + password);
    auto ha2 = md5Hex(method + ":" + uri);
    return md5Hex(ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" + qop + ":" + ha2);
}

auto extractNonce(const ims::sip::SipMessage& response) -> std::string {
    auto serialized = response.toString();
    EXPECT_TRUE(serialized.has_value()) << serialized.error().message;
    auto start = serialized->find("nonce=\"");
    EXPECT_NE(start, std::string::npos);
    start += 7;
    auto end = serialized->find('"', start);
    EXPECT_NE(end, std::string::npos);
    return serialized->substr(start, end - start);
}

auto makeRegister(const std::string& call_id, uint32_t cseq) -> ims::sip::SipMessage {
    auto request = ims::sip::createRequest("REGISTER", "sip:ims.example.com");
    EXPECT_TRUE(request.has_value()) << request.error().message;

    request->setFromHeader("<sip:460112024122023@ims.example.com>;tag=from-1");
    request->setToHeader("<sip:460112024122023@ims.example.com>");
    request->setCallId(call_id);
    request->setCSeq(cseq, "REGISTER");
    request->addVia(std::format("SIP/2.0/UDP 127.0.0.1:5060;branch=z9hG4bK-reg-{}", cseq));
    request->setContact("<sip:460112024122023@127.0.0.1:5060>");
    request->addHeader("Expires", "600");
    return std::move(*request);
}

auto makeAuthorizedRegister(const std::string& call_id,
                            uint32_t cseq,
                            const std::string& nonce) -> ims::sip::SipMessage {
    constexpr auto kUsername = "460112024122023@ims.example.com";
    constexpr auto kRealm = "ims.example.com";
    constexpr auto kPassword = "testpass";
    constexpr auto kUri = "sip:ims.example.com";
    constexpr auto kNc = "00000001";
    constexpr auto kCnonce = "deadbeef";
    constexpr auto kQop = "auth";

    auto response = computeDigestResponse(kUsername, kRealm, kPassword, "REGISTER", kUri,
                                          nonce, kNc, kCnonce, kQop);
    auto raw = std::format(
        "REGISTER {} SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:5060;branch=z9hG4bK-reg-{}\r\n"
        "From: <sip:460112024122023@ims.example.com>;tag=from-1\r\n"
        "To: <sip:460112024122023@ims.example.com>\r\n"
        "Call-ID: {}\r\n"
        "CSeq: {} REGISTER\r\n"
        "Contact: <sip:460112024122023@127.0.0.1:5060>\r\n"
        "Expires: 600\r\n"
        "Authorization: Digest username=\"{}\", realm=\"{}\", nonce=\"{}\", uri=\"{}\", "
        "response=\"{}\", algorithm=MD5, qop={}, nc={}, cnonce=\"{}\"\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        kUri, cseq, call_id, cseq, kUsername, kRealm, nonce, kUri, response, kQop, kNc, kCnonce);

    auto parsed = ims::sip::SipMessage::parse(raw);
    EXPECT_TRUE(parsed.has_value()) << parsed.error().message;
    return std::move(*parsed);
}

auto makeTxn(const ims::sip::SipMessage& request,
             const std::shared_ptr<CapturingTransport>& transport,
             boost::asio::io_context& io) -> std::shared_ptr<ims::sip::ServerTransaction> {
    auto txn_request = request.clone();
    EXPECT_TRUE(txn_request.has_value()) << txn_request.error().message;
    ims::sip::Endpoint source{.address = "127.0.0.1", .port = 5060, .transport = "udp"};
    return std::make_shared<ims::sip::ServerTransaction>(std::move(*txn_request), transport, source, io);
}

} // namespace

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

TEST(MemoryRegistrationStoreTest, LookupPrunesExpiredContacts) {
    ims::registration::MemoryRegistrationStore store;

    RegistrationBinding binding;
    binding.impu = "sip:user@ims.example.com";
    binding.impi = "user@ims.example.com";
    binding.state = RegistrationBinding::State::kRegistered;
    binding.contacts.push_back({
        .contact_uri = "sip:active@10.0.0.1",
        .expires = std::chrono::steady_clock::now() + std::chrono::hours(1),
    });
    binding.contacts.push_back({
        .contact_uri = "sip:expired@10.0.0.2",
        .expires = std::chrono::steady_clock::now() - std::chrono::minutes(1),
    });

    ASSERT_TRUE(store.store(binding).has_value());

    auto lookup = store.lookup("sip:user@ims.example.com");
    ASSERT_TRUE(lookup.has_value()) << lookup.error().message;
    ASSERT_EQ(lookup->contacts.size(), 1u);
    EXPECT_EQ(lookup->contacts[0].contact_uri, "sip:active@10.0.0.1");
}

TEST(MemoryRegistrationStoreTest, LookupRemovesFullyExpiredBinding) {
    ims::registration::MemoryRegistrationStore store;

    RegistrationBinding binding;
    binding.impu = "sip:user@ims.example.com";
    binding.impi = "user@ims.example.com";
    binding.state = RegistrationBinding::State::kRegistered;
    binding.contacts.push_back({
        .contact_uri = "sip:expired@10.0.0.2",
        .expires = std::chrono::steady_clock::now() - std::chrono::minutes(1),
    });

    ASSERT_TRUE(store.store(binding).has_value());

    auto lookup = store.lookup("sip:user@ims.example.com");
    ASSERT_FALSE(lookup.has_value());
    EXPECT_EQ(lookup.error().code, ErrorCode::kRegistrationNotFound);

    auto registered = store.isRegistered("sip:user@ims.example.com");
    ASSERT_TRUE(registered.has_value());
    EXPECT_FALSE(*registered);
}

TEST(UriUtilsTest, NormalizeImpuUriSupportsTelAndSipAliases) {
    EXPECT_EQ(ims::sip::normalize_impu_uri("tel:+8613824122023;phone-context=ims.operator.com"),
              "tel:+8613824122023");
    EXPECT_EQ(ims::sip::normalize_impu_uri("<sip:+8613824122023@IMS.OPERATOR.COM;user=phone>"),
              "sip:+8613824122023@ims.operator.com");
    EXPECT_EQ(ims::sip::normalize_impu_uri("sip:460112024122023@IMS.OPERATOR.COM"),
              "sip:460112024122023@ims.operator.com");
}

TEST(RegistrarAuthTest, DuplicateInitialRegisterDoesNotInvalidateFirstChallenge) {
    boost::asio::io_context io;
    auto transport = std::make_shared<CapturingTransport>();
    auto store = std::make_shared<MockRegistrationStore>();
    auto hss = std::make_shared<MockHssClient>();

    ims::HssAdapterConfig digest_config;
    digest_config.subscribers.push_back({
        .imsi = "460112024122023",
        .tel = "+8613824122023",
        .password = "testpass",
        .realm = "ims.example.com",
    });
    auto digest_store = ims::scscf::make_local_digest_credential_store(digest_config);
    std::vector<std::shared_ptr<ims::scscf::IAuthProvider>> providers{
        std::make_shared<ims::scscf::DigestAuthProvider>(digest_store, "ims.example.com"),
    };
    ims::scscf::Registrar registrar(store, std::move(providers), hss, "ims.example.com");

    const auto not_found = ims::Result<RegistrationBinding>{
        std::unexpected(ims::ErrorInfo{ims::ErrorCode::kRegistrationNotFound, "Not found"})};

    EXPECT_CALL(*store, lookup("sip:460112024122023@ims.example.com"))
        .Times(3)
        .WillRepeatedly(Return(not_found));

    EXPECT_CALL(*hss, serverAssignment(_))
        .WillOnce(Return(ims::Result<ims::diameter::SaaResult>{
            ims::diameter::SaaResult{
                .result_code = 2001,
                .user_profile = {
                    .impu = "sip:460112024122023@ims.example.com",
                    .associated_impus = {"sip:460112024122023@ims.example.com"},
                    .ifcs = {},
                },
            }}));
    EXPECT_CALL(*store, store(_)).WillOnce(Return(ims::VoidResult{}));

    auto first_register = makeRegister("register-race-call", 1);
    registrar.handleRegister(first_register, makeTxn(first_register, transport, io));
    ASSERT_EQ(transport->sent_messages.size(), 1u);
    ASSERT_EQ(transport->sent_messages[0].statusCode(), 401);
    auto first_nonce = extractNonce(transport->sent_messages[0]);

    auto duplicate_register = makeRegister("register-race-call", 2);
    registrar.handleRegister(duplicate_register, makeTxn(duplicate_register, transport, io));
    ASSERT_EQ(transport->sent_messages.size(), 2u);
    ASSERT_EQ(transport->sent_messages[1].statusCode(), 401);
    EXPECT_EQ(extractNonce(transport->sent_messages[1]), first_nonce);

    auto authorized_register = makeAuthorizedRegister("register-race-call", 3, first_nonce);
    registrar.handleRegister(authorized_register, makeTxn(authorized_register, transport, io));
    ASSERT_EQ(transport->sent_messages.size(), 3u);
    EXPECT_EQ(transport->sent_messages[2].statusCode(), 200);
}
