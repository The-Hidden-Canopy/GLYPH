#pragma once

#include "glyph/core/status.hpp"
#include "glyph/optical/surface.hpp"

#include <cstdint>

namespace glyph::sim {

struct ImpairmentConfig {
    std::uint64_t seed = 1U;
    std::uint8_t noise_amplitude = 0U;
    std::uint8_t dropout_percent = 0U;
    std::uint8_t quantization_bits = 8U;
};

struct CaptureReceipt {
    std::uint64_t seed = 0U;
    std::uint64_t pixels_processed = 0U;
    std::uint64_t pixels_dropped = 0U;
    std::uint64_t channels_noised = 0U;

    friend constexpr bool operator==(const CaptureReceipt&,
                                     const CaptureReceipt&) = default;
};

// Applies a deterministic, bounded synthetic impairment chain to an in-memory
// software surface. This is not a camera model and does not produce hardware
// or conformance evidence.
[[nodiscard]] Status simulate_capture(
    const optical::OpticalSurface& source,
    const ImpairmentConfig& config,
    optical::OpticalSurface& captured,
    CaptureReceipt& receipt);

}  // namespace glyph::sim
