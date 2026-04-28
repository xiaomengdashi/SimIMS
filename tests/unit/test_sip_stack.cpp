#define private public
#include "sip/stack.hpp"
#undef private

#include "sip/transport.hpp"

#include <boost/asio/io_context.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

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
        return ims::sip::Endpoint{.address = "127.0.0.1", .port = 5060, .transport = "udp"};
    }

    MessageCallback callback;
    std::vector<ims::sip::SipMessage> sent_messages;
    std::vector<ims::sip::Endpoint> sent_destinations;
};

class TestSipStack final : public ims::sip::SipStack {
public:
    TestSipStack(boost::asio::io_context& io, std::shared_ptr<ims::sip::ITransport> transport)
        : ims::sip::SipStack(io, "127.0.0.1", 0) {
        transport_ = std::move(transport);
        txn_layer_ = std::make_unique<ims::sip::TransactionLayer>(io, transport_);
    }
};

auto makeRequest(const std::string& method) -> ims::sip::SipMessage {
    auto parsed = ims::sip::SipMessage::parse(
        method + " sip:bob@ims.example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:5090;branch=z9hG4bK-stack-test\r\n"
        "From: <sip:alice@ims.example.com>;tag=from\r\n"
        "To: <sip:bob@ims.example.com>\r\n"
        "Call-ID: stack-test\r\n"
        "CSeq: 1 " + method + "\r\n"
        "Content-Length: 0\r\n\r\n");
    EXPECT_TRUE(parsed.has_value()) << parsed.error().message;
    return std::move(*parsed);
}

auto makeTxn(const ims::sip::SipMessage& request,
             const std::shared_ptr<CapturingTransport>& transport,
             boost::asio::io_context& io) -> std::shared_ptr<ims::sip::ServerTransaction> {
    auto clone = request.clone();
    EXPECT_TRUE(clone.has_value()) << clone.error().message;
    return std::make_shared<ims::sip::ServerTransaction>(
        std::move(*clone),
        transport,
        ims::sip::Endpoint{.address = "127.0.0.1", .port = 5090, .transport = "udp"},
        io);
}

} // namespace

TEST(SipStackTest, DispatchCopiesHandlerAndRunsOutsideLock) {
    boost::asio::io_context io;
    auto transport = std::make_shared<CapturingTransport>();
    TestSipStack stack(io, transport);
    std::atomic<int> calls{0};

    stack.onRequest("OPTIONS", [&](std::shared_ptr<ims::sip::ServerTransaction>, ims::sip::SipMessage&) {
        stack.onRequest("OPTIONS", [&](std::shared_ptr<ims::sip::ServerTransaction>, ims::sip::SipMessage&) {
            calls.fetch_add(10);
        });
        calls.fetch_add(1);
    });

    auto first = makeRequest("OPTIONS");
    stack.handleRequest(makeTxn(first, transport, io));
    auto second = makeRequest("OPTIONS");
    stack.handleRequest(makeTxn(second, transport, io));

    EXPECT_EQ(calls.load(), 11);
}

TEST(SipStackTest, DefaultHandlerHandlesUnknownMethod) {
    boost::asio::io_context io;
    auto transport = std::make_shared<CapturingTransport>();
    TestSipStack stack(io, transport);
    std::atomic<bool> called{false};

    stack.onDefaultRequest([&](std::shared_ptr<ims::sip::ServerTransaction>, ims::sip::SipMessage&) {
        called.store(true);
    });

    auto request = makeRequest("MESSAGE");
    stack.handleRequest(makeTxn(request, transport, io));

    EXPECT_TRUE(called.load());
    EXPECT_TRUE(transport->sent_messages.empty());
}

TEST(SipStackTest, UnknownMethodWithoutDefaultReturns405) {
    boost::asio::io_context io;
    auto transport = std::make_shared<CapturingTransport>();
    TestSipStack stack(io, transport);

    auto request = makeRequest("MESSAGE");
    stack.handleRequest(makeTxn(request, transport, io));

    ASSERT_EQ(transport->sent_messages.size(), 1u);
    EXPECT_EQ(transport->sent_messages[0].statusCode(), 405);
}

TEST(SipStackTest, ConcurrentRegistrationAndDispatchDoesNotRace) {
    boost::asio::io_context io;
    auto transport = std::make_shared<CapturingTransport>();
    TestSipStack stack(io, transport);
    std::atomic<int> calls{0};

    std::jthread writer([&] {
        for (int i = 0; i < 100; ++i) {
            stack.onRequest("OPTIONS", [&calls](std::shared_ptr<ims::sip::ServerTransaction>, ims::sip::SipMessage&) {
                calls.fetch_add(1, std::memory_order_relaxed);
            });
        }
    });

    std::vector<std::jthread> readers;
    readers.reserve(4);
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&] {
            for (int j = 0; j < 50; ++j) {
                auto request = makeRequest("OPTIONS");
                stack.handleRequest(makeTxn(request, transport, io));
            }
        });
    }
}
