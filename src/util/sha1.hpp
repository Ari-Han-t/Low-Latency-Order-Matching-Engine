#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace lom::util {

std::array<uint8_t, 20> sha1(const std::string& input);

} // namespace lom::util
