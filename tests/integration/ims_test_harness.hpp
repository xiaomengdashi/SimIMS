#pragma once

#include "sip_message_samples.hpp"

#include "sms/smsc_service.hpp"

#define private public
#include "i-cscf/icscf_service.hpp"
#include "p-cscf/pcscf_service.hpp"
#include "s-cscf/digest_credential_store.hpp"
#include "s-cscf/scscf_service.hpp"
#undef private

#include "mocks/mock_hss_client.hpp"
#include "mocks/mock_pcf_client.hpp"
#include "mocks/mock_rtpengine_client.hpp"
#include "sip/memory_store.hpp"
#include "sip/message.hpp"
#include "sip/reg_event_notifier.hpp"
#include "sip/stack.hpp"
#include "sip/transaction.hpp"

#include <boost/asio/io_context.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <format>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ims::integration {

enum class Node {
    kPcscf,
    kIcscf,
    kScscf,
    kSmsc,
    kCallerUe,
    kCalleeUe,
};

class ImsTestHarness;

class FabricTransport final : public ims::sip::ITransport {
public:
    FabricTransport(ImsTestHarness& harness, Node node, ims::sip::Endpoint local);

    auto send(const ims::sip::SipMessage& msg, const ims::sip::Endpoint& dest) -> ims::VoidResult override;
    void setMessageCallback(MessageCallback cb) override;
    auto start() -> ims::VoidResult override;
    void stop() override;
    auto localEndpoint() const -> ims::sip::Endpoint override;

private:
    ImsTestHarness& harness_;
    Node node_;
    ims::sip::Endpoint local_;
    MessageCallback callback_;
};

class NullRegEventNotifier final : public ims::sip::IRegEventNotifier {
public:
    auto start() -> ims::VoidResult override { return {}; }
    auto sendInitialNotify(const ims::sip::InitialRegNotifyContext&) -> ims::VoidResult override { return {}; }
    void shutdown() override {}
};

class FakeSipStack final : public ims::sip::SipStack {
public:
    FakeSipStack(boost::asio::io_context& io,
                 const std::string& bind_addr,
                 ims::Port port,
                 std::shared_ptr<ims::sip::ITransport> transport)
        : ims::sip::SipStack(io, bind_addr, port)
        , injected_transport_(std::move(transport)) {
        assignTransport(injected_transport_);
    }

private:
    std::shared_ptr<ims::sip::ITransport> injected_transport_;
};

class ImsTestHarness {
public:
    ImsTestHarness() {
        using ::testing::_;
        using ::testing::Invoke;
        using ::testing::Return;

        ims::HssAdapterConfig hss_cfg;
        hss_cfg.subscribers = {
            {.imsi = "460112024122023",
             .tel = "+8613824122023",
             .password = kPassword,
             .realm = kDomain},
            {.imsi = "460112024122024",
             .tel = "+8613824122024",
             .password = kPassword,
             .realm = kDomain},
        };
        digest_store_ = std::make_shared<ims::scscf::LocalDigestCredentialStore>(hss_cfg);
        store_ = std::make_shared<ims::registration::MemoryRegistrationStore>();

        ON_CALL(*hss_, userAuthorization(_))
            .WillByDefault(Return(ims::Result<ims::diameter::UaaResult>{ims::diameter::UaaResult{
                .result_code = 2001,
                .assigned_scscf = std::format("sip:127.0.0.1:{};transport=udp", kScscfPort),
            }}));
        ON_CALL(*hss_, serverAssignment(_))
            .WillByDefault(Invoke([](const ims::diameter::SarParams& params) {
                return ims::Result<ims::diameter::SaaResult>{ims::diameter::SaaResult{
                    .result_code = 2001,
                    .user_profile = {
                        .impu = params.impu,
                        .associated_impus = {params.impu},
                    },
                }};
            }));
        ON_CALL(*hss_, locationInfo(_))
            .WillByDefault(Return(ims::Result<ims::diameter::LiaResult>{ims::diameter::LiaResult{
                .result_code = 2001,
                .assigned_scscf = std::format("sip:127.0.0.1:{};transport=udp", kScscfPort),
            }}));
        ON_CALL(*pcf_, authorizeSession(_))
            .WillByDefault(Return(ims::Result<ims::diameter::AaaResult>{ims::diameter::AaaResult{
                .result_code = 2001,
            }}));
        ON_CALL(*rtpengine_, offer(_, _, _))
            .WillByDefault(Return(ims::Result<ims::media::RtpengineResult>{ims::media::RtpengineResult{
                .sdp = "",
            }}));
        ON_CALL(*rtpengine_, answer(_, _, _))
            .WillByDefault(Return(ims::Result<ims::media::RtpengineResult>{ims::media::RtpengineResult{
                .sdp = "",
            }}));

        wireNodes();
    }

    auto start() -> ims::VoidResult {
        if (auto result = smsc_->start(); !result) {
            return result;
        }
        if (auto result = pcscf_->start(); !result) {
            return result;
        }
        if (auto result = icscf_->start(); !result) {
            return result;
        }
        return scscf_->start();
    }

    void stop() {
        scscf_->stop();
        icscf_->stop();
        pcscf_->stop();
        smsc_->stop();
    }

    void sendUeRaw(Node ue, std::string_view raw) {
        auto parsed = ims::sip::SipMessage::parse(std::string(raw));
        ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
        deliverToPcscf(std::move(*parsed), endpoint(ue));
        drainIo();
    }

    void sendUeResponse(Node ue, const ims::sip::SipMessage& request, int status, std::string_view reason) {
        auto response = ims::sip::createResponse(request, status, std::string(reason));
        ASSERT_TRUE(response.has_value()) << response.error().message;
        deliverToPcscf(std::move(*response), endpoint(ue));
        drainIo();
    }

    auto findResponse(Node ue, std::string_view call_id, int status) const
        -> const ims::sip::SipMessage* {
        for (const auto& msg : ueMessages(ue)) {
            if (msg.isResponse() && msg.callId() == call_id && msg.statusCode() == status) {
                return &msg;
            }
        }
        return nullptr;
    }

    auto findRequest(Node ue, std::string_view method, std::string_view call_id) const
        -> const ims::sip::SipMessage* {
        for (const auto& msg : ueMessages(ue)) {
            if (msg.isRequest() && msg.method() == method && msg.callId() == call_id) {
                return &msg;
            }
        }
        return nullptr;
    }

    auto findMessageWithRpAck(Node ue) const -> const ims::sip::SipMessage* {
        for (const auto& msg : ueMessages(ue)) {
            if (!msg.isRequest() || msg.method() != "MESSAGE") {
                continue;
            }
            const auto body = msg.body().value_or(std::string{});
            if (body.size() >= 1 && static_cast<uint8_t>(body[0]) == 0x02) {
                return &msg;
            }
        }
        return nullptr;
    }

    void deliver(const ims::sip::Endpoint& dest,
                 ims::sip::SipMessage msg,
                 const ims::sip::Endpoint& source) {
        if (dest.port == kPcscfPort) {
            deliverToPcscf(std::move(msg), source);
            return;
        }
        if (dest.port == kIcscfPort) {
            deliverToIcscf(std::move(msg), source);
            return;
        }
        if (dest.port == kScscfPort) {
            deliverToScscf(std::move(msg), source);
            return;
        }
        if (dest.port == kSmscPort) {
            deliverToSmsc(std::move(msg), source);
            return;
        }
        if (dest.port == kCallerUePort) {
            ue_outbox_[Node::kCallerUe].push_back(std::move(msg));
            return;
        }
        if (dest.port == kCalleeUePort) {
            ue_outbox_[Node::kCalleeUe].push_back(std::move(msg));
            return;
        }

        ADD_FAILURE() << "ImsTestHarness has no handler for destination "
                      << dest.address << ":" << dest.port;
    }

    void drainIo() {
        for (int i = 0; i < 64; ++i) {
            io_.poll();
        }
    }

    auto ueMessages(Node ue) const -> const std::vector<ims::sip::SipMessage>& {
        static const std::vector<ims::sip::SipMessage> k_empty;
        const auto it = ue_outbox_.find(ue);
        if (it == ue_outbox_.end()) {
            return k_empty;
        }
        return it->second;
    }

    void registerUe(Node ue,
                      const std::string& impi,
                      const std::string& impu,
                      const std::string& call_id,
                      ims::Port ue_port) {
        sendUeRaw(ue, register_challenge_raw(impu, call_id, ue_port, 1));

        const auto& responses = ueMessages(ue);
        ASSERT_FALSE(responses.empty());
        const auto& challenge = responses.back();
        ASSERT_TRUE(challenge.isResponse());
        EXPECT_EQ(challenge.statusCode(), 401);

        const auto serialized = challenge.toString();
        ASSERT_TRUE(serialized.has_value()) << serialized.error().message;
        std::smatch match;
        static const std::regex nonce_pattern("nonce=\"([^\"]+)\"");
        ASSERT_TRUE(std::regex_search(*serialized, match, nonce_pattern));
        const auto nonce = match[1].str();

        sendUeRaw(ue, register_authorized_raw(impi, impu, call_id, ue_port, 2, nonce));

        const auto& final_responses = ueMessages(ue);
        ASSERT_FALSE(final_responses.empty());
        EXPECT_EQ(final_responses.back().statusCode(), 200);
    }

    auto hasRequestTo(Node ue, std::string_view method, std::string_view call_id) const -> bool {
        for (const auto& msg : ueMessages(ue)) {
            if (!msg.isRequest()) {
                continue;
            }
            if (msg.method() == method && msg.callId() == call_id) {
                return true;
            }
        }
        return false;
    }

    auto store() const -> std::shared_ptr<ims::registration::MemoryRegistrationStore> {
        return store_;
    }

    static auto endpoint(Node node) -> ims::sip::Endpoint {
        switch (node) {
        case Node::kPcscf:
            return {.address = "127.0.0.1", .port = kPcscfPort, .transport = "udp"};
        case Node::kIcscf:
            return {.address = "127.0.0.1", .port = kIcscfPort, .transport = "udp"};
        case Node::kScscf:
            return {.address = "127.0.0.1", .port = kScscfPort, .transport = "udp"};
        case Node::kSmsc:
            return {.address = "127.0.0.1", .port = kSmscPort, .transport = "udp"};
        case Node::kCallerUe:
            return {.address = "127.0.0.1", .port = kCallerUePort, .transport = "udp"};
        case Node::kCalleeUe:
            return {.address = "127.0.0.1", .port = kCalleeUePort, .transport = "udp"};
        }
        return {};
    }

private:
    void deliverToPcscf(ims::sip::SipMessage msg, const ims::sip::Endpoint& source) {
        pcscf_->sip_stack_->transactionLayer().processMessage(std::move(msg), source);
    }

    void deliverToIcscf(ims::sip::SipMessage msg, const ims::sip::Endpoint& source) {
        icscf_->sip_stack_->transactionLayer().processMessage(std::move(msg), source);
    }

    void deliverToScscf(ims::sip::SipMessage msg, const ims::sip::Endpoint& source) {
        scscf_->sip_stack_->transactionLayer().processMessage(std::move(msg), source);
    }

    void deliverToSmsc(ims::sip::SipMessage msg, const ims::sip::Endpoint& source) {
        smsc_->processMessage(std::move(msg), source);
    }

    void wireNodes() {
        pcscf_transport_ = std::make_shared<FabricTransport>(*this, Node::kPcscf, endpoint(Node::kPcscf));
        icscf_transport_ = std::make_shared<FabricTransport>(*this, Node::kIcscf, endpoint(Node::kIcscf));
        scscf_transport_ = std::make_shared<FabricTransport>(*this, Node::kScscf, endpoint(Node::kScscf));
        smsc_transport_ = std::make_shared<FabricTransport>(*this, Node::kSmsc, endpoint(Node::kSmsc));
        caller_transport_ = std::make_shared<FabricTransport>(*this, Node::kCallerUe, endpoint(Node::kCallerUe));
        callee_transport_ = std::make_shared<FabricTransport>(*this, Node::kCalleeUe, endpoint(Node::kCalleeUe));

        ims::PcscfConfig pcscf_cfg;
        pcscf_cfg.listen_addr = "127.0.0.1";
        pcscf_cfg.listen_port = kPcscfPort;
        pcscf_cfg.core_entry = {.address = "127.0.0.1", .port = kIcscfPort, .transport = "udp"};
        pcscf_cfg.core_peers = {
            {.address = "127.0.0.1", .port = kIcscfPort, .transport = "udp"},
            {.address = "127.0.0.1", .port = kScscfPort, .transport = "udp"},
        };

        ims::IcscfConfig icscf_cfg;
        icscf_cfg.listen_addr = "127.0.0.1";
        icscf_cfg.listen_port = kIcscfPort;
        icscf_cfg.hss.realm = kDomain;
        icscf_cfg.local_scscf = {.address = "127.0.0.1", .port = kScscfPort, .transport = "udp"};

        ims::ScscfConfig scscf_cfg;
        scscf_cfg.listen_addr = "127.0.0.1";
        scscf_cfg.listen_port = kScscfPort;
        scscf_cfg.domain = kDomain;
        scscf_cfg.auth_mode = "digest_only";
        scscf_cfg.exosip.enabled = false;
        scscf_cfg.smsc = ims::SmscSettings{
            .endpoint = {.address = "127.0.0.1", .port = kSmscPort, .transport = "udp"},
            .psi = kSmscPsi,
        };

        pcscf_ = std::make_unique<ims::pcscf::PcscfService>(
            pcscf_cfg, io_, pcf_, rtpengine_, pcscf_cfg.core_entry.address, pcscf_cfg.core_entry.port);
        icscf_ = std::make_unique<ims::icscf::IcscfService>(icscf_cfg, io_, hss_);
        scscf_ = std::make_unique<ims::scscf::ScscfService>(
            scscf_cfg,
            io_,
            hss_,
            store_,
            digest_store_,
            std::make_unique<NullRegEventNotifier>());

        pcscf_->sip_stack_ = std::make_unique<FakeSipStack>(io_, "127.0.0.1", kPcscfPort, pcscf_transport_);
        icscf_->sip_stack_ = std::make_unique<FakeSipStack>(io_, "127.0.0.1", kIcscfPort, icscf_transport_);
        scscf_->sip_stack_ = std::make_unique<FakeSipStack>(io_, "127.0.0.1", kScscfPort, scscf_transport_);
        scscf_->session_router_ = std::make_unique<ims::scscf::SessionRouter>(
            scscf_->store_,
            *scscf_->sip_stack_,
            std::nullopt,
            ims::sip::Endpoint{.address = "127.0.0.1", .port = kSmscPort, .transport = "udp"});

        ims::SmscConfig smsc_cfg{
            .listen_addr = "127.0.0.1",
            .listen_port = kSmscPort,
            .transport = "udp",
            .psi = kSmscPsi,
            .scscf = {.address = "127.0.0.1", .port = kScscfPort, .transport = "udp"},
        };
        smsc_ = std::make_unique<ims::sms::SmscService>(smsc_cfg, io_, smsc_transport_);
    }

    boost::asio::io_context io_;
    std::shared_ptr<ims::test::MockHssClient> hss_ = std::make_shared<ims::test::MockHssClient>();
    std::shared_ptr<ims::test::MockPcfClient> pcf_ = std::make_shared<ims::test::MockPcfClient>();
    std::shared_ptr<ims::test::MockRtpengineClient> rtpengine_ =
        std::make_shared<ims::test::MockRtpengineClient>();
    std::shared_ptr<ims::registration::MemoryRegistrationStore> store_;
    std::shared_ptr<ims::scscf::LocalDigestCredentialStore> digest_store_;

    std::shared_ptr<FabricTransport> pcscf_transport_;
    std::shared_ptr<FabricTransport> icscf_transport_;
    std::shared_ptr<FabricTransport> scscf_transport_;
    std::shared_ptr<FabricTransport> smsc_transport_;
    std::shared_ptr<FabricTransport> caller_transport_;
    std::shared_ptr<FabricTransport> callee_transport_;

    std::unique_ptr<ims::pcscf::PcscfService> pcscf_;
    std::unique_ptr<ims::icscf::IcscfService> icscf_;
    std::unique_ptr<ims::scscf::ScscfService> scscf_;
    std::unique_ptr<ims::sms::SmscService> smsc_;

    std::unordered_map<Node, std::vector<ims::sip::SipMessage>> ue_outbox_;
};

inline FabricTransport::FabricTransport(ImsTestHarness& harness, Node node, ims::sip::Endpoint local)
    : harness_(harness)
    , node_(node)
    , local_(std::move(local)) {}

inline auto FabricTransport::send(const ims::sip::SipMessage& msg, const ims::sip::Endpoint& dest)
    -> ims::VoidResult {
    auto clone = msg.clone();
    if (!clone) {
        return std::unexpected(clone.error());
    }
    harness_.deliver(dest, std::move(*clone), local_);
    return {};
}

inline void FabricTransport::setMessageCallback(MessageCallback cb) {
    callback_ = std::move(cb);
}

inline auto FabricTransport::start() -> ims::VoidResult {
    return {};
}

inline void FabricTransport::stop() {}

inline auto FabricTransport::localEndpoint() const -> ims::sip::Endpoint {
    return local_;
}

} // namespace ims::integration
