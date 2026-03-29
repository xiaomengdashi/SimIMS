#include <any>
#include <sstream>

#define private public
#include "p-cscf/pcscf_service.hpp"
#undef private

#include "mocks/mock_pcf_client.hpp"
#include "mocks/mock_rtpengine_client.hpp"
#include "sip/message.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using ::testing::_;
using ::testing::StrictMock;
using ::testing::Truly;

auto makeInviteResponse(int status, std::string_view reason, std::string_view call_id,
                        std::string_view from_tag, std::string_view to_tag,
                        const std::optional<std::string>& sdp) -> ims::sip::SipMessage {
    auto parsed = ims::sip::SipMessage::parse(
        std::string("SIP/2.0 ") + std::to_string(status) + " " + std::string(reason) + "\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:5060;branch=z9hG4bK-test\r\n"
        "To: <sip:callee@ims.local>;tag=" + std::string(to_tag) + "\r\n"
        "From: <sip:caller@ims.local>;tag=" + std::string(from_tag) + "\r\n"
        "Call-ID: " + std::string(call_id) + "\r\n"
        "CSeq: 1 INVITE\r\n"
        "Content-Length: 0\r\n"
        "\r\n");
    EXPECT_TRUE(parsed.has_value()) << parsed.error().message;
    if (!parsed) {
        throw std::runtime_error("Failed to parse SIP response in test fixture");
    }

    if (sdp) {
        parsed->setBody(*sdp, "application/sdp");
    }
    return std::move(*parsed);
}

class PcscfServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        ims::PcscfConfig cfg;
        cfg.listen_addr = "127.0.0.1";
        cfg.listen_port = 0;

        pcf_ = std::make_shared<StrictMock<ims::test::MockPcfClient>>();
        rtpengine_ = std::make_shared<StrictMock<ims::test::MockRtpengineClient>>();
        service_ = std::make_unique<ims::pcscf::PcscfService>(
            cfg, io_, pcf_, rtpengine_, "127.0.0.1", 5061);
    }

    boost::asio::io_context io_;
    std::shared_ptr<StrictMock<ims::test::MockPcfClient>> pcf_;
    std::shared_ptr<StrictMock<ims::test::MockRtpengineClient>> rtpengine_;
    std::unique_ptr<ims::pcscf::PcscfService> service_;
};

TEST_F(PcscfServiceTest, Invite183WithSdpTriggersRtpengineAnswerAndRewritesSdp) {
    static const std::string kCallId = "call-183";
    static const std::string kFromTag = "from-a";
    static const std::string kToTag = "to-b";
    static const std::string kOriginalSdp = "v=0\r\no=- 1 1 IN IP4 10.0.0.1\r\ns=-\r\n";
    static const std::string kRewrittenSdp = "v=0\r\no=- 2 2 IN IP4 127.0.0.1\r\ns=-\r\n";

    service_->media_sessions_.createSession(kCallId, kFromTag);
    auto response = makeInviteResponse(183, "Session Progress", kCallId, kFromTag, kToTag, kOriginalSdp);

    EXPECT_CALL(
        *rtpengine_,
        answer(Truly([](const ims::media::MediaSession& session) {
                   return session.call_id == "call-183" &&
                          session.from_tag == "from-a" &&
                          session.to_tag == "to-b";
               }),
               kOriginalSdp, _))
        .WillOnce([](const ims::media::MediaSession&, const std::string&, const ims::media::RtpengineFlags&) {
            return ims::media::RtpengineResult{.sdp = "v=0\r\no=- 2 2 IN IP4 127.0.0.1\r\ns=-\r\n"};
        });

    service_->onInviteResponse(response);

    auto body = response.body();
    ASSERT_TRUE(body.has_value());
    EXPECT_EQ(*body, kRewrittenSdp);

    auto state = service_->media_sessions_.getSession(kCallId);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->session.to_tag, kToTag);
    EXPECT_EQ(state->callee_sdp, kOriginalSdp);
}

TEST_F(PcscfServiceTest, Invite486DoesNotCallRtpengineAnswer) {
    static const std::string kCallId = "call-486";
    static const std::string kFromTag = "from-a";
    static const std::string kOriginalSdp = "v=0\r\ns=-\r\n";

    service_->media_sessions_.createSession(kCallId, kFromTag);
    auto response = makeInviteResponse(486, "Busy Here", kCallId, kFromTag, "to-b", kOriginalSdp);

    EXPECT_CALL(*rtpengine_, answer(_, _, _)).Times(0);

    service_->onInviteResponse(response);

    auto body = response.body();
    ASSERT_TRUE(body.has_value());
    EXPECT_EQ(*body, kOriginalSdp);
}

TEST_F(PcscfServiceTest, Invite200WithoutSdpDoesNotCallRtpengineAnswer) {
    static const std::string kCallId = "call-200-no-sdp";
    static const std::string kFromTag = "from-a";

    service_->media_sessions_.createSession(kCallId, kFromTag);
    auto response = makeInviteResponse(200, "OK", kCallId, kFromTag, "to-b", std::nullopt);

    EXPECT_CALL(*rtpengine_, answer(_, _, _)).Times(0);

    service_->onInviteResponse(response);

    EXPECT_FALSE(response.body().has_value());
}

TEST_F(PcscfServiceTest, Invite183WithoutTrackedSessionDoesNotCallRtpengineAnswer) {
    auto response = makeInviteResponse(
        183, "Session Progress", "missing-call", "from-a", "to-b", std::string("v=0\r\ns=-\r\n"));

    EXPECT_CALL(*rtpengine_, answer(_, _, _)).Times(0);

    service_->onInviteResponse(response);
}

} // namespace
