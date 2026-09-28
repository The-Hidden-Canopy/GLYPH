#pragma once

#include "glyph/core/status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace glyph {

inline constexpr std::size_t kMaxRgb8PayloadBytes = 16U << 20U;

struct Rgb8Cell {
    std::uint8_t red = 0U;
    std::uint8_t green = 0U;
    std::uint8_t blue = 0U;

    friend constexpr bool operator==(const Rgb8Cell&, const Rgb8Cell&) =
        default;
};

struct Rgb8Observation {
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
};

struct Rgb8Calibration {
    Rgb8Observation black{};
    Rgb8Observation one{1.0F, 1.0F, 1.0F};
    float min_confidence = 0.15F;
};

struct Rgb8Decision {
    std::uint8_t symbol = 0U;
    bool erasure = true;
    std::array<float, 3> confidence{};
};

[[nodiscard]] Status encode_rgb8_payload(
    std::span<const std::byte> payload,
    std::vector<Rgb8Cell>& cells,
    std::size_t max_payload_bytes = kMaxRgb8PayloadBytes);

[[nodiscard]] Status decode_rgb8_payload(
    std::span<const Rgb8Cell> cells,
    std::uint64_t expected_bytes,
    std::vector<std::byte>& payload,
    std::size_t max_payload_bytes = kMaxRgb8PayloadBytes);

[[nodiscard]] Status classify_rgb8_cell(
    const Rgb8Observation& observation,
    const Rgb8Calibration& calibration,
    Rgb8Decision& decision);

}  // namespace glyph

