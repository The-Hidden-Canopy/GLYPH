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
    std::array<std::array<float, 3>, 3> inverse_mixing{{
        {{1.0F, 0.0F, 0.0F}},
        {{0.0F, 1.0F, 0.0F}},
        {{0.0F, 0.0F, 1.0F}},
    }};
    bool use_mixing_matrix = false;
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

// Decodes already-classified RGB8 decisions. Erased cells contribute no
// guessed bits; every logical byte touched by an erased cell is reported.
[[nodiscard]] Status decode_rgb8_decisions(
    std::span<const Rgb8Decision> decisions,
    std::uint64_t expected_bytes,
    std::vector<std::byte>& payload,
    std::vector<std::uint32_t>& erasure_positions,
    std::size_t max_payload_bytes = kMaxRgb8PayloadBytes);

// Fits the inverse observed-channel mixing matrix from black, red-only,
// green-only, and blue-only calibration patches.
[[nodiscard]] Status fit_rgb8_calibration(
    const Rgb8Observation& black,
    const Rgb8Observation& red,
    const Rgb8Observation& green,
    const Rgb8Observation& blue,
    float min_confidence,
    Rgb8Calibration& calibration);

[[nodiscard]] Status classify_rgb8_cell(
    const Rgb8Observation& observation,
    const Rgb8Calibration& calibration,
    Rgb8Decision& decision);

}  // namespace glyph
