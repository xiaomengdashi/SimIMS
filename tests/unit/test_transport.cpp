#include "sip/transport.hpp"
#include "sip/message.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

constexpr auto kOptionsMessage =
    "OPTIONS sip:bob@ims.example.com SIP/2.0\r\n"
    "Via: SIP/2.0/UDP 127.0.0.1:5060;branch=z9hG4bK-transport-test\r\n"
    "From: <sip:alice@ims.example.com>;tag=from\r\n"
    "To: <sip:bob@ims.example.com>\r\n"
    "Call-ID: transport-test\r\n"
    "CSeq: 1 OPTIONS\r\n"
    "Content-Length: 0\r\n\r\n";

class TcpCountingServer {
public:
    explicit TcpCountingServer(boost::asio::io_context& io)
        : acceptor_(io, {boost::asio::ip::tcp::v4(), 0}) {
        accept();
    }

    auto port() const -> ims::Port {
        return static_cast<ims::Port>(acceptor_.local_endpoint().port());
    }

    auto accepts() const -> int {
        return accepts_.load();
    }

private:
    void accept() {
        acceptor_.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
            if (!ec) {
                accepts_.fetch_add(1);
                connections_.push_back(std::make_shared<boost::asio::ip::tcp::socket>(std::move(socket)));
            }
            accept();
        });
    }

    boost::asio::ip::tcp::acceptor acceptor_;
    std::atomic<int> accepts_{0};
    std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>> connections_;
};

} // namespace

TEST(TransportTest, UdpSendSerializesConcurrentSocketAccess) {
    boost::asio::io_context io;
    ims::sip::UdpTransport transport(io, "127.0.0.1", 0);
    ASSERT_TRUE(transport.start().has_value());

    boost::asio::ip::udp::socket receiver(io, {boost::asio::ip::udp::v4(), 0});
    ims::sip::Endpoint dest{
        .address = "127.0.0.1",
        .port = static_cast<ims::Port>(receiver.local_endpoint().port()),
        .transport = "udp",
    };

    auto message = ims::sip::SipMessage::parse(kOptionsMessage);
    ASSERT_TRUE(message.has_value()) << message.error().message;

    constexpr int kSenderCount = 8;
    constexpr int kSendsPerSender = 20;
    std::vector<std::jthread> senders;
    senders.reserve(kSenderCount);
    std::atomic<int> failures{0};
    for (int i = 0; i < kSenderCount; ++i) {
        senders.emplace_back([&] {
            for (int j = 0; j < kSendsPerSender; ++j) {
                auto result = transport.send(*message, dest);
                if (!result) {
                    failures.fetch_add(1);
                }
            }
        });
    }

    senders.clear();
    EXPECT_EQ(failures.load(), 0);
    transport.stop();
}

TEST(TransportTest, TcpConcurrentSendReusesSingleConnectionPerEndpoint) {
    boost::asio::io_context server_io;
    TcpCountingServer server(server_io);
    std::jthread server_thread([&](std::stop_token) {
        server_io.run();
    });

    boost::asio::io_context client_io;
    ims::sip::TcpTransport transport(client_io, "127.0.0.1", 0);

    auto message = ims::sip::SipMessage::parse(kOptionsMessage);
    ASSERT_TRUE(message.has_value()) << message.error().message;

    ims::sip::Endpoint dest{
        .address = "127.0.0.1",
        .port = server.port(),
        .transport = "tcp",
    };

    constexpr int kSenderCount = 8;
    std::vector<std::jthread> senders;
    senders.reserve(kSenderCount);
    std::atomic<int> failures{0};
    for (int i = 0; i < kSenderCount; ++i) {
        senders.emplace_back([&] {
            auto result = transport.send(*message, dest);
            if (!result) {
                failures.fetch_add(1);
            }
        });
    }

    senders.clear();
    std::this_thread::sleep_for(100ms);

    EXPECT_EQ(failures.load(), 0);
    EXPECT_EQ(server.accepts(), 1);

    transport.stop();
    server_io.stop();
}
