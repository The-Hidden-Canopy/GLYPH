#pragma once

#include "glyph/frame/frame.hpp"
#include "glyph/phy/rgb8.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace glyph::optical {

inline constexpr std::size_t kMaxMp0SurfacePixels = 16U << 20U;
inline constexpr std::uint32_t kMp0SafeMarginCells = 2U;
inline constexpr std::uint32_t kMp0AnchorCells = 3U;
inline constexpr std::uint32_t kMp0PilotColumns = 2U;
inline constexpr std::uint32_t kMp0ControlRows = 7U;
inline constexpr std::uint32_t kMp0CalibrationRows = 2U;
inline constexpr std::size_t kMp0ControlCells = kFrameHeaderBytes * 4U;
inline constexpr std::size_t kMp0ControlRepetitions = 2U;

enum class SurfaceAspect : std::uint8_t {
    landscape_16_9 = 0,
    portrait_9_16,
    square,
};

struct Rgba8 {
    std::uint8_t red = 0U;
    std::uint8_t green = 0U;
    std::uint8_t blue = 0U;
    std::uint8_t alpha = 255U;

    friend constexpr bool operator==(const Rgba8&, const Rgba8&) = default;
};

struct PixelRect {
    std::uint32_t x = 0U;
    std::uint32_t y = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;

    friend constexpr bool operator==(const PixelRect&, const PixelRect&) =
        default;
};

struct Mp0SurfaceConfig {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint16_t cell_pitch = 6U;
    SurfaceAspect aspect = SurfaceAspect::landscape_16_9;
    // The numeric profile registry is intentionally not assigned here.
    std::uint8_t profile_id = 0U;
    std::uint32_t frame_seq = 0U;
    std::uint32_t hold_index = 0U;
};

struct SurfaceLayout {
    PixelRect safe_area{};
    PixelRect control_band{};
    PixelRect payload_area{};
    PixelRect calibration_band{};
    std::uint32_t grid_columns = 0U;
    std::uint32_t grid_rows = 0U;
    std::uint32_t payload_columns = 0U;
    std::uint32_t payload_rows = 0U;
    std::size_t payload_cell_capacity = 0U;
};

struct OpticalSurface {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<Rgba8> pixels;
    std::uint32_t frame_seq = 0U;
    std::uint32_t hold_index = 0U;
};

// Calculates the deterministic MP0 geometry. The safe area is cell-aligned;
// any incomplete outer pixels remain black and are not part of the protocol
// surface.
[[nodiscard]] Status calculate_mp0_layout(const Mp0SurfaceConfig& config,
                                          SurfaceLayout& layout);

// Renders one software MP0 surface. The control band carries the canonical
// 64-byte frame header as four 2-bit RGB4 symbols per byte, repeated across
// the control band. Payload cells are already encoded RGB8 cells, normally
// produced by Rgb8TileCodec. This is a source/unit and synthetic-rendering
// primitive, not camera or conformance evidence.
[[nodiscard]] Status render_mp0_surface(
    const Mp0SurfaceConfig& config,
    const FrameHeader& header,
    std::span<const Rgb8Cell> payload_cells,
    OpticalSurface& surface);

}  // namespace glyph::optical
