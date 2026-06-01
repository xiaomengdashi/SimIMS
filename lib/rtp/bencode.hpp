#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>
#include <stdexcept>

namespace ims::media {

/// Bencode value types for rtpengine NG protocol
struct BencodeValue;
using BencodeDict = std::map<std::string, BencodeValue>;
using BencodeList = std::vector<BencodeValue>;
using BencodeVariant = std::variant<std::string, int64_t, BencodeList, BencodeDict>;

struct BencodeValue : BencodeVariant {
    using BencodeVariant::BencodeVariant;
};

/// Encode a bencode value to string
std::string bencode_encode(const BencodeValue& value);

/// Decode a bencode string to value
/// Throws std::runtime_error on invalid input
BencodeValue bencode_decode(const std::string& data);

} // namespace ims::media
