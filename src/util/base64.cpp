#include "base64.hpp"

namespace lom::util {

namespace {
constexpr char k_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64_encode(const uint8_t* data, std::size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 2 < len) {
        const uint32_t triple = (static_cast<uint32_t>(data[i]) << 16)
                              | (static_cast<uint32_t>(data[i + 1]) << 8)
                              | static_cast<uint32_t>(data[i + 2]);
        out.push_back(k_alphabet[(triple >> 18) & 0x3F]);
        out.push_back(k_alphabet[(triple >> 12) & 0x3F]);
        out.push_back(k_alphabet[(triple >> 6) & 0x3F]);
        out.push_back(k_alphabet[triple & 0x3F]);
        i += 3;
    }

    if (i < len) {
        uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(k_alphabet[(triple >> 18) & 0x3F]);
        if (i + 1 < len) {
            triple |= static_cast<uint32_t>(data[i + 1]) << 8;
            out.push_back(k_alphabet[(triple >> 12) & 0x3F]);
            out.push_back(k_alphabet[(triple >> 6) & 0x3F]);
            out.push_back('=');
        } else {
            out.push_back(k_alphabet[(triple >> 12) & 0x3F]);
            out.push_back('=');
            out.push_back('=');
        }
    }

    return out;
}

std::string base64_encode(const std::vector<uint8_t>& data) {
    return base64_encode(data.data(), data.size());
}

} // namespace lom::util

