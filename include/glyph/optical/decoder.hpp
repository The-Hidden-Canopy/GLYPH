#pragma once

#include "glyph/core/status.hpp"
#include "glyph/frame/frame.hpp"
#include "glyph/optical/surface.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace glyph::optical {

struct SurfaceDecodeConfig {
    std::uint16_t cell_pitch = 6U;
    SurfaceAspect aspect = SurfaceAspect::landscape_16_9;
    std::uint8_t profile_id = 0U;
    std::size_t max_payload_cells = kMaxMp0SurfacePixels;
};

struct SurfaceDecodeResult {
    bool surface_found = false;
    bool accepted = false;
    float geometry_confidence = 0.0F;
    float calibration_confidence = 0.0F;
    std::uint32_t logical_frame_seq = 0U;
    std::uint32_t control_repetitions_verified = 0U;
    std::uint32_t payload_cells_erased = 0U;
    FrameHeader header{};
    std::vector<Rgb8Cell> payload_cells;
};

// Decodes an axis-aligned, canonical software surface. It intentionally does
// not estimate homography, tolerate optical noise, or turn ambiguous pixels
// into guesses; those are later receiver work packages.
[[nodiscard]] Status decode_mp0_surface(
    const OpticalSurface& surface,
    const SurfaceDecodeConfig& config,
    SurfaceDecodeResult& result);

}  // namespace glyph::optical
