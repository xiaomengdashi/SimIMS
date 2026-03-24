#include "bencode.hpp"

#include <sstream>
#include <stdexcept>

namespace ims::media {

namespace {

void encode_impl(const BencodeValue& value, std::ostringstream& out) {
    if (auto* s = std::get_if<std::string>(&value)) {
        out << s->size() << ':' << *s;
    } else if (auto* i = std::get_if<int64_t>(&value)) {
        out << 'i' << *i << 'e';
    } else if (auto* list = std::get_if<BencodeList>(&value)) {
        out << 'l';
        for (const auto& item : *list) {
            encode_impl(item, out);
        }
        out << 'e';
    } else if (auto* dict = std::get_if<BencodeDict>(&value)) {
        out << 'd';
        // std::map is already sorted by key
        for (const auto& [key, val] : *dict) {
            out << key.size() << ':' << key;
            encode_impl(val, out);
        }
        out << 'e';
    }
}

class BencodeParser {
public:
    explicit BencodeParser(const std::string& data)
        : data_(data), pos_(0) {}

    auto parse() -> BencodeValue {
        if (pos_ >= data_.size()) {
            throw std::runtime_error("Unexpected end of bencode data");
        }

        char c = data_[pos_];

        if (c == 'i') {
            return parseInteger();
        } else if (c == 'l') {
            return parseList();
        } else if (c == 'd') {
            return parseDict();
        } else if (c >= '0' && c <= '9') {
            return parseString();
        }

        throw std::runtime_error(
            std::string("Unexpected character in bencode: '") + c + "'");
    }

private:
    auto parseString() -> BencodeValue {
        // Read length
        auto colon_pos = data_.find(':', pos_);
        if (colon_pos == std::string::npos) {
            throw std::runtime_error("Missing colon in bencode string");
        }

        auto len = std::stoul(data_.substr(pos_, colon_pos - pos_));
        pos_ = colon_pos + 1;

        if (pos_ + len > data_.size()) {
            throw std::runtime_error("String length exceeds data");
        }

        auto result = data_.substr(pos_, len);
        pos_ += len;
        return BencodeValue{result};
    }

    auto parseInteger() -> BencodeValue {
        ++pos_;  // skip 'i'
        auto end_pos = data_.find('e', pos_);
        if (end_pos == std::string::npos) {
            throw std::runtime_error("Missing 'e' in bencode integer");
        }

        auto num = std::stoll(data_.substr(pos_, end_pos - pos_));
        pos_ = end_pos + 1;
        return BencodeValue{num};
    }

    auto parseList() -> BencodeValue {
        ++pos_;  // skip 'l'
        BencodeList list;

        while (pos_ < data_.size() && data_[pos_] != 'e') {
            list.push_back(parse());
        }

        if (pos_ >= data_.size()) {
            throw std::runtime_error("Missing 'e' in bencode list");
        }
        ++pos_;  // skip 'e'

        return BencodeValue{std::move(list)};
    }

    auto parseDict() -> BencodeValue {
        ++pos_;  // skip 'd'
        BencodeDict dict;

        while (pos_ < data_.size() && data_[pos_] != 'e') {
            // Key must be a string
            auto key_val = parse();
            auto* key = std::get_if<std::string>(&key_val);
            if (!key) {
                throw std::runtime_error("Dict key must be a string");
            }

            auto value = parse();
            dict[*key] = std::move(value);
        }

        if (pos_ >= data_.size()) {
            throw std::runtime_error("Missing 'e' in bencode dict");
        }
        ++pos_;  // skip 'e'

        return BencodeValue{std::move(dict)};
    }

    const std::string& data_;
    size_t pos_;
};

} // anonymous namespace

std::string bencode_encode(const BencodeValue& value) {
    std::ostringstream out;
    encode_impl(value, out);
    return out.str();
}

BencodeValue bencode_decode(const std::string& data) {
    BencodeParser parser(data);
    return parser.parse();
}

} // namespace ims::media
