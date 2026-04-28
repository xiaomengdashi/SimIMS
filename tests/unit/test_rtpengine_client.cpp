#include "rtp/bencode.hpp"
#include "rtp/rtpengine_client_impl.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using boost::asio::ip::udp;
using ims::media::BencodeDict;
using ims::media::BencodeValue;
using ims::media::MediaSession;
using ims::media::RtpengineClientImpl;
using namespace std::chrono_literals;

class FakeRtpengineServer {
public:
    struct ReceivedRequest {
        std::string cookie;
        BencodeDict dict;
        udp::endpoint client_endpoint;
    };

    FakeRtpengineServer()
        : socket_(io_, udp::endpoint(udp::v4(), 0))
    {
        boost::system::error_code ec;
        socket_.non_blocking(true, ec);
        if (ec) {
            throw std::runtime_error("failed to configure fake rtpengine socket");
        }
    }

    ~FakeRtpengineServer() {
        boost::system::error_code ec;
        socket_.close(ec);
    }

    auto port() const -> uint16_t {
        return socket_.local_endpoint().port();
    }

    auto receiveRequest(std::chrono::milliseconds timeout = 1s) -> ReceivedRequest {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::array<char, 65535> buffer{};

        while (std::chrono::steady_clock::now() < deadline) {
            boost::system::error_code ec;
            udp::endpoint sender;
            const auto bytes = socket_.receive_from(boost::asio::buffer(buffer), sender, 0, ec);
            if (!ec) {
                return parseRequest(std::string(buffer.data(), bytes), sender);
            }
            if (ec != boost::asio::error::would_block && ec != boost::asio::error::try_again) {
                throw std::runtime_error("failed to receive fake rtpengine request: " + ec.message());
            }
            std::this_thread::sleep_for(5ms);
        }

        throw std::runtime_error("timed out waiting for fake rtpengine request");
    }

    void sendResponse(const udp::endpoint& client_endpoint,
                      const std::string& cookie,
                      const BencodeDict& dict) {
        const auto payload = cookie + " " + ims::media::bencode_encode(BencodeValue{dict});
        boost::system::error_code ec;
        socket_.send_to(boost::asio::buffer(payload), client_endpoint, 0, ec);
        if (ec) {
            throw std::runtime_error("failed to send fake rtpengine response: " + ec.message());
        }
    }

private:
    static auto parseRequest(const std::string& payload, const udp::endpoint& sender)
        -> ReceivedRequest {
        const auto delimiter = payload.find(' ');
        if (delimiter == std::string::npos) {
            throw std::runtime_error("invalid fake rtpengine request payload");
        }

        auto decoded = ims::media::bencode_decode(payload.substr(delimiter + 1));
        auto* dict = std::get_if<BencodeDict>(&decoded);
        if (!dict) {
            throw std::runtime_error("fake rtpengine request payload is not a dict");
        }

        return ReceivedRequest{
            .cookie = payload.substr(0, delimiter),
            .dict = std::move(*dict),
            .client_endpoint = sender,
        };
    }

    boost::asio::io_context io_;
    udp::socket socket_;
};

auto dictString(const BencodeDict& dict, const std::string& key) -> std::string {
    const auto it = dict.find(key);
    if (it == dict.end()) {
        throw std::runtime_error("missing dict key: " + key);
    }

    const auto* value = std::get_if<std::string>(&it->second);
    if (!value) {
        throw std::runtime_error("dict key is not a string: " + key);
    }

    return *value;
}

auto decodeDict(const std::string& payload) -> BencodeDict {
    auto decoded = ims::media::bencode_decode(payload);
    auto* dict = std::get_if<BencodeDict>(&decoded);
    if (!dict) {
        throw std::runtime_error("expected bencode dict response");
    }
    return *dict;
}

auto makeSession(std::string call_id) -> MediaSession {
    return MediaSession{
        .call_id = std::move(call_id),
        .from_tag = "from-tag",
        .to_tag = "to-tag",
    };
}

auto makeResponseDict(const std::string& marker, const std::string& call_id) -> BencodeDict {
    BencodeDict dict;
    dict["result"] = BencodeValue{std::string("ok")};
    dict["marker"] = BencodeValue{marker};
    dict["call-id"] = BencodeValue{call_id};
    return dict;
}

TEST(RtpengineClientTest, ConcurrentRequestsDispatchResponsesByCookie) {
    boost::asio::io_context io;
    FakeRtpengineServer server;
    RtpengineClientImpl client(io, "127.0.0.1", server.port());

    const auto session_one = makeSession("call-1");
    const auto session_two = makeSession("call-2");

    auto first = std::async(std::launch::async, [&] {
        return client.query(session_one);
    });
    auto second = std::async(std::launch::async, [&] {
        return client.query(session_two);
    });

    auto request_a = server.receiveRequest();
    auto request_b = server.receiveRequest();

    server.sendResponse(
        request_b.client_endpoint,
        request_b.cookie,
        makeResponseDict("response-" + dictString(request_b.dict, "call-id"),
                         dictString(request_b.dict, "call-id")));
    server.sendResponse(
        request_a.client_endpoint,
        request_a.cookie,
        makeResponseDict("response-" + dictString(request_a.dict, "call-id"),
                         dictString(request_a.dict, "call-id")));

    auto first_result = first.get();
    ASSERT_TRUE(first_result.has_value()) << first_result.error().message;
    auto first_dict = decodeDict(*first_result);
    EXPECT_EQ(dictString(first_dict, "call-id"), "call-1");
    EXPECT_EQ(dictString(first_dict, "marker"), "response-call-1");

    auto second_result = second.get();
    ASSERT_TRUE(second_result.has_value()) << second_result.error().message;
    auto second_dict = decodeDict(*second_result);
    EXPECT_EQ(dictString(second_dict, "call-id"), "call-2");
    EXPECT_EQ(dictString(second_dict, "marker"), "response-call-2");
}

TEST(RtpengineClientTest, LateResponseDoesNotPoisonNextRequest) {
    boost::asio::io_context io;
    FakeRtpengineServer server;
    RtpengineClientImpl client(io, "127.0.0.1", server.port());

    const auto expired_session = makeSession("call-timeout");
    const auto fresh_session = makeSession("call-fresh");

    auto expired = std::async(std::launch::async, [&] {
        return client.query(expired_session);
    });
    auto expired_request = server.receiveRequest();

    auto expired_result = expired.get();
    ASSERT_FALSE(expired_result.has_value());
    EXPECT_NE(expired_result.error().message.find("Timed out waiting for rtpengine response"),
              std::string::npos);

    auto fresh = std::async(std::launch::async, [&] {
        return client.query(fresh_session);
    });
    auto fresh_request = server.receiveRequest();

    server.sendResponse(
        expired_request.client_endpoint,
        expired_request.cookie,
        makeResponseDict("late-response", dictString(expired_request.dict, "call-id")));
    server.sendResponse(
        fresh_request.client_endpoint,
        fresh_request.cookie,
        makeResponseDict("fresh-response", dictString(fresh_request.dict, "call-id")));

    auto fresh_result = fresh.get();
    ASSERT_TRUE(fresh_result.has_value()) << fresh_result.error().message;
    auto fresh_dict = decodeDict(*fresh_result);
    EXPECT_EQ(dictString(fresh_dict, "call-id"), "call-fresh");
    EXPECT_EQ(dictString(fresh_dict, "marker"), "fresh-response");
}

TEST(RtpengineClientTest, TimeoutReturnsError) {
    boost::asio::io_context io;
    FakeRtpengineServer server;
    RtpengineClientImpl client(io, "127.0.0.1", server.port());

    const auto session = makeSession("call-timeout-only");

    auto start = std::chrono::steady_clock::now();
    auto pending = std::async(std::launch::async, [&] {
        return client.query(session);
    });
    auto ignored_request = server.receiveRequest();
    (void)ignored_request;

    auto result = pending.get();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message.find("Timed out waiting for rtpengine response"),
              std::string::npos);
    EXPECT_GE(elapsed, 1900ms);
}

} // namespace
