#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace glyph {

[[nodiscard]] std::uint32_t crc32c(
    std::span<const std::byte> bytes) noexcept;

}  // namespace glyph

