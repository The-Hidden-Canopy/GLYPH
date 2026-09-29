#include "glyph/frame/crc32c.hpp"

#include <cstddef>

namespace glyph {

std::uint32_t crc32c(const std::span<const std::byte> bytes) noexcept {
    constexpr std::uint32_t polynomial = 0x82f63b78U;
    std::uint32_t crc = 0xffffffffU;

    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (unsigned int bit = 0U; bit < 8U; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                0U - static_cast<std::uint32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (polynomial & mask);
        }
    }

    return ~crc;
}

}  // namespace glyph

