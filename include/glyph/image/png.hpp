#pragma once

#include "glyph/core/status.hpp"
#include "glyph/optical/surface.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace glyph::image {

inline constexpr std::size_t kMaxPngEncodedBytes = 96U << 20U;

// Encodes and decodes the canonical offline image form used by the simulator:
// 8-bit RGBA, non-interlaced PNG. The input and output vectors are transactional
// on failure and bounded by kMaxMp0SurfacePixels/kMaxPngEncodedBytes.
[[nodiscard]] Status encode_rgba8_png(
    std::uint32_t width,
    std::uint32_t height,
    std::span<const optical::Rgba8> pixels,
    std::vector<std::byte>& encoded);

[[nodiscard]] Status decode_rgba8_png(
    std::span<const std::byte> encoded,
    std::uint32_t& width,
    std::uint32_t& height,
    std::vector<optical::Rgba8>& pixels);

}  // namespace glyph::image
