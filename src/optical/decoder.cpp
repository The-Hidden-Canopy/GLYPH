#include "glyph/optical/decoder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>

namespace glyph::optical {
namespace {

constexpr Rgba8 kBlack{0U, 0U, 0U, 255U};
constexpr Rgba8 kWhite{255U, 255U, 255U, 255U};
constexpr Rgba8 kRed{255U, 0U, 0U, 255U};
constexpr Rgba8 kGreen{0U, 255U, 0U, 255U};
constexpr Rgba8 kBlue{0U, 0U, 255U, 255U};

Rgba8 sample_cell(const OpticalSurface& surface,
                  const std::uint16_t pitch,
                  const std::uint32_t cell_x,
                  const std::uint32_t cell_y) noexcept {
    const auto pixel_x = static_cast<std::size_t>(cell_x) * pitch + pitch / 2U;
    const auto pixel_y = static_cast<std::size_t>(cell_y) * pitch + pitch / 2U;
    return surface.pixels[pixel_y * surface.width + pixel_x];
}

bool is_binary_channel(const std::uint8_t value) noexcept {
    return value == 0U || value == 0xffU;
}

bool is_exact_rgb8(const Rgba8& pixel) noexcept {
    return pixel.alpha == 255U && is_binary_channel(pixel.red) &&
           is_binary_channel(pixel.green) && is_binary_channel(pixel.blue);
}

bool is_exact_rgb4(const Rgba8& pixel) noexcept {
    return pixel == kBlack || pixel == kRed || pixel == kGreen ||
           pixel == kBlue;
}

bool decode_rgb4_symbol(const Rgba8& pixel, std::uint8_t& symbol) noexcept {
    if (pixel == kBlack) {
        symbol = 0U;
        return true;
    }
    if (pixel == kRed) {
        symbol = 1U;
        return true;
    }
    if (pixel == kGreen) {
        symbol = 2U;
        return true;
    }
    if (pixel == kBlue) {
        symbol = 3U;
        return true;
    }
    return false;
}

bool validate_anchor(const OpticalSurface& surface,
                     const std::uint16_t pitch,
                     const std::uint32_t first_x,
                     const std::uint32_t first_y,
                     const Rgba8 orientation_color) noexcept {
    for (std::uint32_t row = 0U; row < kMp0AnchorCells; ++row) {
        for (std::uint32_t column = 0U; column < kMp0AnchorCells; ++column) {
            const auto pattern_index = row * kMp0AnchorCells + column;
            const auto expected = row == 1U && column == 1U
                                      ? orientation_color
                                      : (kMp0AnchorPattern[pattern_index] != 0U
                                             ? kWhite
                                             : kBlack);
            if (sample_cell(surface, pitch, first_x + column, first_y + row) !=
                expected) {
                return false;
            }
        }
    }
    return true;
}

bool decode_header_copy(const OpticalSurface& surface,
                        const SurfaceLayout& layout,
                        const std::uint16_t pitch,
                        const std::size_t copy_index,
                        std::array<std::byte, kFrameHeaderBytes>& bytes) {
    const auto columns = layout.control_band.width / pitch;
    const auto first_x = layout.control_band.x / pitch;
    const auto first_y = layout.control_band.y / pitch;
    const auto copy_offset = copy_index * kMp0ControlCells;
    for (std::size_t byte_index = 0U; byte_index < kFrameHeaderBytes;
         ++byte_index) {
        std::uint8_t value = 0U;
        for (std::size_t symbol_index = 0U; symbol_index < 4U;
             ++symbol_index) {
            const auto index = copy_offset + byte_index * 4U + symbol_index;
            const auto cell_x = first_x + index % columns;
            const auto cell_y = first_y + index / columns;
            std::uint8_t symbol = 0U;
            const auto pixel = sample_cell(surface, pitch, cell_x, cell_y);
            if (!is_exact_rgb4(pixel) || !decode_rgb4_symbol(pixel, symbol)) {
                return false;
            }
            value = static_cast<std::uint8_t>((value << 2U) | symbol);
        }
        bytes[byte_index] = static_cast<std::byte>(value);
    }
    return true;
}

bool decode_payload_cell(const Rgba8& pixel, Rgb8Cell& cell) noexcept {
    if (!is_exact_rgb8(pixel)) {
        return false;
    }
    cell = Rgb8Cell{pixel.red, pixel.green, pixel.blue};
    return true;
}

}  // namespace

Status decode_mp0_surface(const OpticalSurface& surface,
                          const SurfaceDecodeConfig& config,
                          SurfaceDecodeResult& result) {
    if (surface.width == 0U || surface.height == 0U || config.cell_pitch == 0U ||
        config.max_payload_cells == 0U) {
        return Status::invalid_argument;
    }
    if (static_cast<std::size_t>(surface.height) >
        std::numeric_limits<std::size_t>::max() /
            static_cast<std::size_t>(surface.width)) {
        return Status::resource_limit;
    }
    const auto pixel_count = static_cast<std::size_t>(surface.width) *
                             static_cast<std::size_t>(surface.height);
    if (surface.pixels.size() != pixel_count) {
        return Status::invalid_argument;
    }

    SurfaceLayout layout;
    const auto layout_status = calculate_mp0_layout(
        Mp0SurfaceConfig{surface.width, surface.height, config.cell_pitch,
                         config.aspect, config.profile_id, surface.frame_seq,
                         surface.hold_index},
        layout);
    if (layout_status != Status::ok) {
        return layout_status;
    }
    if (layout.payload_cell_capacity > config.max_payload_cells) {
        return Status::resource_limit;
    }

    const auto first_x = layout.safe_area.x / config.cell_pitch;
    const auto first_y = layout.safe_area.y / config.cell_pitch;
    const auto safe_columns = layout.safe_area.width / config.cell_pitch;
    const auto safe_rows = layout.safe_area.height / config.cell_pitch;
    if (!validate_anchor(surface, config.cell_pitch, first_x, first_y,
                         kRed) ||
        !validate_anchor(surface, config.cell_pitch,
                         first_x + safe_columns - kMp0AnchorCells, first_y,
                         kGreen) ||
        !validate_anchor(surface, config.cell_pitch, first_x,
                         first_y + safe_rows - kMp0AnchorCells, kBlue) ||
        !validate_anchor(surface, config.cell_pitch,
                         first_x + safe_columns - kMp0AnchorCells,
                         first_y + safe_rows - kMp0AnchorCells, kWhite)) {
        return Status::integrity;
    }

    std::array<std::byte, kFrameHeaderBytes> first_header{};
    std::array<std::byte, kFrameHeaderBytes> second_header{};
    if (!decode_header_copy(surface, layout, config.cell_pitch, 0U,
                            first_header) ||
        !decode_header_copy(surface, layout, config.cell_pitch, 1U,
                            second_header) ||
        first_header != second_header) {
        return Status::integrity;
    }

    FrameHeader header;
    const auto header_status = decode_frame_header(first_header, header);
    if (header_status != Status::ok || header.profile_id != config.profile_id ||
        header.cell_pitch != config.cell_pitch ||
        header.frame_seq != surface.frame_seq) {
        return header_status == Status::ok ? Status::protocol : header_status;
    }

    const auto calibration_columns = layout.calibration_band.width /
                                     config.cell_pitch;
    const auto calibration_x = layout.calibration_band.x / config.cell_pitch;
    const auto calibration_y = layout.calibration_band.y / config.cell_pitch;
    const std::array<Rgba8, 5> calibration_colors{
        kBlack, kRed, kGreen, kBlue, kWhite};
    for (std::size_t patch = 0U; patch < calibration_colors.size(); ++patch) {
        const auto column = (calibration_columns * patch) /
                            calibration_colors.size();
        if (sample_cell(surface, config.cell_pitch, calibration_x + column,
                        calibration_y) != calibration_colors[patch]) {
            return Status::integrity;
        }
    }

    try {
        SurfaceDecodeResult candidate;
        candidate.surface_found = true;
        candidate.accepted = true;
        candidate.geometry_confidence = 1.0F;
        candidate.calibration_confidence = 1.0F;
        candidate.logical_frame_seq = header.frame_seq;
        candidate.control_repetitions_verified = 2U;
        candidate.header = header;
        candidate.payload_cells.resize(layout.payload_cell_capacity);
        const auto payload_x = layout.payload_area.x / config.cell_pitch;
        const auto payload_y = layout.payload_area.y / config.cell_pitch;
        for (std::size_t index = 0U; index < candidate.payload_cells.size();
             ++index) {
            const auto row = static_cast<std::uint32_t>(
                index / layout.payload_columns);
            const auto column = static_cast<std::uint32_t>(
                index % layout.payload_columns);
            if (!decode_payload_cell(
                    sample_cell(surface, config.cell_pitch, payload_x + column,
                                payload_y + row),
                    candidate.payload_cells[index])) {
                candidate.payload_cells[index] = Rgb8Cell{};
                ++candidate.payload_cells_erased;
            }
        }
        result = std::move(candidate);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

}  // namespace glyph::optical
