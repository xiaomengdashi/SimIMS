#include <gtest/gtest.h>
#include <string>
#include <map>
#include <vector>
#include <variant>

// Forward declaration - we'll test the bencode module
// These tests verify the bencode encoding/decoding used by rtpengine NG protocol

namespace ims::media {

// Bencode value types
struct BencodeValue;
using BencodeDict = std::map<std::string, BencodeValue>;
using BencodeList = std::vector<BencodeValue>;
using BencodeVariant = std::variant<std::string, int64_t, BencodeList, BencodeDict>;

struct BencodeValue : BencodeVariant {
    using BencodeVariant::BencodeVariant;
};

// Encode functions
std::string bencode_encode(const BencodeValue& value);
BencodeValue bencode_decode(const std::string& data);

} // namespace ims::media

// Placeholder tests - will compile once bencode.cpp is implemented
TEST(BencodeTest, EncodeString) {
    using namespace ims::media;
    BencodeValue val{std::string("hello")};
    auto encoded = bencode_encode(val);
    EXPECT_EQ(encoded, "5:hello");
}

TEST(BencodeTest, EncodeInteger) {
    using namespace ims::media;
    BencodeValue val{int64_t(42)};
    auto encoded = bencode_encode(val);
    EXPECT_EQ(encoded, "i42e");
}

TEST(BencodeTest, EncodeDict) {
    using namespace ims::media;
    BencodeDict dict;
    dict["command"] = BencodeValue{std::string("ping")};
    BencodeValue val{dict};
    auto encoded = bencode_encode(val);
    // Dict keys are sorted: "command" -> "ping"
    EXPECT_EQ(encoded, "d7:command4:pinge");
}

TEST(BencodeTest, DecodeString) {
    using namespace ims::media;
    auto val = bencode_decode("5:hello");
    ASSERT_TRUE(std::holds_alternative<std::string>(val));
    EXPECT_EQ(std::get<std::string>(val), "hello");
}

TEST(BencodeTest, DecodeInteger) {
    using namespace ims::media;
    auto val = bencode_decode("i42e");
    ASSERT_TRUE(std::holds_alternative<int64_t>(val));
    EXPECT_EQ(std::get<int64_t>(val), 42);
}

TEST(BencodeTest, DecodeDict) {
    using namespace ims::media;
    auto val = bencode_decode("d7:command4:pinge");
    ASSERT_TRUE(std::holds_alternative<BencodeDict>(val));
    auto& dict = std::get<BencodeDict>(val);
    ASSERT_TRUE(dict.count("command"));
    EXPECT_EQ(std::get<std::string>(dict.at("command")), "ping");
}

TEST(BencodeTest, RoundTrip) {
    using namespace ims::media;
    BencodeDict dict;
    dict["command"] = BencodeValue{std::string("offer")};
    dict["call-id"] = BencodeValue{std::string("test-call-123")};

    BencodeDict sdp_dict;
    sdp_dict["sdp"] = BencodeValue{std::string("v=0\r\n")};
    dict["sdp"] = BencodeValue{sdp_dict};

    BencodeValue original{dict};
    auto encoded = bencode_encode(original);
    auto decoded = bencode_decode(encoded);

    ASSERT_TRUE(std::holds_alternative<BencodeDict>(decoded));
    auto& result = std::get<BencodeDict>(decoded);
    EXPECT_EQ(std::get<std::string>(result.at("command")), "offer");
    EXPECT_EQ(std::get<std::string>(result.at("call-id")), "test-call-123");
}
