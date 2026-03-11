#include "sha1.hpp"

#include <array>
#include <vector>

namespace lom::util {

namespace {

inline uint32_t left_rotate(uint32_t value, uint32_t bits) {
    return (value << bits) | (value >> (32u - bits));
}

} // namespace

std::array<uint8_t, 20> sha1(const std::string& input) {
    std::vector<uint8_t> message(input.begin(), input.end());
    const uint64_t original_bits = static_cast<uint64_t>(message.size()) * 8ull;

    message.push_back(0x80);
    while ((message.size() % 64) != 56) {
        message.push_back(0x00);
    }

    for (int i = 7; i >= 0; --i) {
        message.push_back(static_cast<uint8_t>((original_bits >> (i * 8)) & 0xFF));
    }

    uint32_t h0 = 0x67452301u;
    uint32_t h1 = 0xEFCDAB89u;
    uint32_t h2 = 0x98BADCFEu;
    uint32_t h3 = 0x10325476u;
    uint32_t h4 = 0xC3D2E1F0u;

    std::array<uint32_t, 80> w{};

    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64) {
        for (int i = 0; i < 16; ++i) {
            const std::size_t offset = chunk + (i * 4);
            w[i] = (static_cast<uint32_t>(message[offset]) << 24)
                 | (static_cast<uint32_t>(message[offset + 1]) << 16)
                 | (static_cast<uint32_t>(message[offset + 2]) << 8)
                 | static_cast<uint32_t>(message[offset + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = left_rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0;
            uint32_t k = 0;

            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }

            const uint32_t temp = left_rotate(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = left_rotate(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<uint8_t, 20> digest{};
    const uint32_t words[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        digest[i * 4] = static_cast<uint8_t>((words[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<uint8_t>((words[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<uint8_t>((words[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<uint8_t>(words[i] & 0xFF);
    }
    return digest;
}

} // namespace lom::util
