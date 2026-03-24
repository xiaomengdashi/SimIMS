#include "sip/exosip_context.hpp"
#include "sip/reg_event_notifier.hpp"

#include <gtest/gtest.h>

namespace {

auto make_exosip_config() -> ims::ExosipConfig {
    ims::ExosipConfig config;
    config.enabled = true;
    config.listen_addr = "127.0.0.1";
    config.listen_port = 0;
    config.transport = "udp";
    config.user_agent = "SimIMS-Test";
    config.event_poll_ms = 10;
    return config;
}

auto make_notify_context() -> ims::sip::InitialRegNotifyContext {
    return ims::sip::InitialRegNotifyContext{
        .request_uri = "sip:user@ims.example.com",
        .from_header = "<sip:scscf@ims.example.com>;tag=from-tag",
        .to_header = "<sip:user@ims.example.com>;tag=to-tag",
        .call_id = "call-id-1",
        .cseq = 2,
        .event = "reg",
        .subscription_state = "active;expires=300",
        .route_set = {},
        .contact = "<sip:scscf@127.0.0.1>",
        .body = "<reginfo/>",
        .content_type = "application/reginfo+xml"
    };
}

} // namespace

TEST(ExosipContextTest, EnsureStartedRejectsInvalidTransport) {
    auto config = make_exosip_config();
    config.transport = "bogus";

    ims::sip::ExosipContext context(config);
    auto result = context.ensureStarted();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ims::ErrorCode::kConfigInvalidValue);
    EXPECT_EQ(result.error().detail, "bogus");
}

TEST(ExosipContextTest, BuildRequestCreatesBasicNotifyMessage) {
    auto config = make_exosip_config();
    config.transport = "UDP";

    ims::sip::ExosipContext context(config);
    auto result = context.buildRequest(
        "NOTIFY",
        "sip:user@ims.example.com",
        "<sip:scscf@ims.example.com>;tag=from-tag");

    ASSERT_TRUE(result.has_value()) << result.error().message;

    auto& request = *result;
    EXPECT_TRUE(request.isRequest());
    EXPECT_EQ(request.method(), "NOTIFY");
    EXPECT_EQ(request.requestUri(), "sip:user@ims.example.com");
    EXPECT_NE(request.fromHeader().find("scscf@ims.example.com"), std::string::npos);
    EXPECT_NE(request.toHeader().find("user@ims.example.com"), std::string::npos);
    EXPECT_FALSE(request.callId().empty());
    EXPECT_EQ(request.cseqMethod(), "NOTIFY");
}

TEST(ExosipRegEventNotifierTest, ApplyInitialNotifyContextSetsNotifyFields) {
    auto context = make_notify_context();
    context.route_set = {
        "<sip:edge1.ims.example.com;lr>",
        "<sip:edge2.ims.example.com;lr>"
    };

    auto request_result = ims::sip::createRequest("NOTIFY", context.request_uri);
    ASSERT_TRUE(request_result.has_value()) << request_result.error().message;

    auto& notify = *request_result;
    ims::sip::applyInitialRegNotifyContext(notify, context);

    EXPECT_EQ(notify.requestUri(), context.request_uri);
    EXPECT_EQ(notify.callId(), context.call_id);
    EXPECT_EQ(notify.cseq(), context.cseq);
    EXPECT_EQ(notify.cseqMethod(), "NOTIFY");
    EXPECT_EQ(notify.getHeader("Event"), context.event);
    EXPECT_EQ(notify.getHeader("Subscription-State"), context.subscription_state);
    EXPECT_EQ(notify.maxForwards(), 70);

    auto contact = notify.contact();
    ASSERT_TRUE(contact.has_value());
    EXPECT_EQ(*contact, context.contact);

    auto serialized = notify.toString();
    ASSERT_TRUE(serialized.has_value()) << serialized.error().message;
    EXPECT_NE(serialized->find("Route: <sip:edge1.ims.example.com;lr>"), std::string::npos);
    EXPECT_NE(serialized->find("<sip:edge2.ims.example.com;lr>"), std::string::npos);

    auto body = notify.body();
    ASSERT_TRUE(body.has_value());
    EXPECT_EQ(*body, context.body);
    EXPECT_NE(serialized->find("Content-Type: application/reginfo+xml"), std::string::npos);
    EXPECT_NE(serialized->find(context.body), std::string::npos);
}

TEST(ExosipRegEventNotifierTest, DisabledModeSkipsStartAndNotify) {
    auto config = make_exosip_config();
    config.enabled = false;
    config.transport = "bogus";

    ims::sip::ExosipRegEventNotifier notifier(config);

    auto start_result = notifier.start();
    EXPECT_TRUE(start_result.has_value()) << start_result.error().message;

    auto send_result = notifier.sendInitialNotify(make_notify_context());
    EXPECT_TRUE(send_result.has_value()) << send_result.error().message;

    notifier.shutdown();
}

TEST(ExosipRegEventNotifierTest, SendInitialNotifyPropagatesExosipErrors) {
    auto config = make_exosip_config();
    config.transport = "bogus";

    ims::sip::ExosipRegEventNotifier notifier(config);
    auto result = notifier.sendInitialNotify(make_notify_context());

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ims::ErrorCode::kConfigInvalidValue);
    EXPECT_EQ(result.error().detail, "bogus");
}
