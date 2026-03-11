#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lom::util {

std::string base64_encode(const uint8_t* data, std::size_t len);
std::string base64_encode(const std::vector<uint8_t>& data);

} // namespace lom::util

