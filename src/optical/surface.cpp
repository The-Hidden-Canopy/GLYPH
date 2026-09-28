#include "glyph/optical/surface.hpp"

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

constexpr std::array<std::uint8_t, 9> kAnchorPattern{
    1U, 1U, 1U,
    1U, 0U, 0U,
    1U, 0U, 1U,
};

bool aspect_matches(const Mp0SurfaceConfig& config) {
    switch (config.aspect) {
        case SurfaceAspect::landscape_16_9:
            return config.width >= config.height;
        case SurfaceAspect::portrait_9_16:
            return config.height >= config.width;
        case SurfaceAspect::square:
            return config.width == config.height;
    }
    return false;
}

bool valid_rgb8_cell(const Rgb8Cell& cell) {
    return (cell.red == 0U || cell.red == 0xffU) &&
           (cell.green == 0U || cell.green == 0xffU) &&
           (cell.blue == 0U || cell.blue == 0xffU);
}

Rgba8 rgba_for_rgb8_cell(const Rgb8Cell& cell) {
    return Rgba8{cell.red, cell.green, cell.blue, 255U};
}

void paint_cell(std::vector<Rgba8>& pixels,
                const std::uint32_t width,
                const std::uint16_t pitch,
                const std::uint32_t cell_x,
                const std::uint32_t cell_y,
                const Rgba8 color) {
    const auto origin_x = static_cast<std::size_t>(cell_x) * pitch;
    const auto origin_y = static_cast<std::size_t>(cell_y) * pitch;
    for (std::uint32_t y = 0U; y < pitch; ++y) {
        const auto row = (origin_y + y) * width;
        for (std::uint32_t x = 0U; x < pitch; ++x) {
            pixels[row + origin_x + x] = color;
        }
    }
}

void paint_anchor(std::vector<Rgba8>& pixels,
                  const std::uint32_t width,
                  const std::uint16_t pitch,
                  const std::uint32_t first_cell_x,
                  const std::uint32_t first_cell_y,
                  const Rgba8 orientation_color) {
    for (std::uint32_t row = 0U; row < kMp0AnchorCells; ++row) {
        for (std::uint32_t column = 0U; column < kMp0AnchorCells; ++column) {
            const auto pattern_index = row * kMp0AnchorCells + column;
            paint_cell(pixels, width, pitch, first_cell_x + column,
                       first_cell_y + row,
                       kAnchorPattern[pattern_index] != 0U ? kWhite : kBlack);
        }
    }
    paint_cell(pixels, width, pitch, first_cell_x + 1U, first_cell_y + 1U,
               orientation_color);
}

void paint_pilots(std::vector<Rgba8>& pixels,
                  const std::uint32_t width,
                  const std::uint16_t pitch,
                  const SurfaceLayout& layout,
                  const std::uint32_t first_cell_y,
                  const std::uint32_t rows) {
    constexpr std::array<Rgba8, 6> kPilotPattern{
        kBlack, kWhite, kRed, kGreen, kBlue, kWhite};
    const auto left_x = layout.safe_area.x / pitch + kMp0AnchorCells;
    const auto right_x = layout.safe_area.x / pitch +
                         (layout.safe_area.width / pitch) -
                         kMp0AnchorCells - kMp0PilotColumns;
    for (std::uint32_t row = 0U; row < rows; ++row) {
        for (std::uint32_t column = 0U; column < kMp0PilotColumns; ++column) {
            const auto color = kPilotPattern[(row + column) %
                                             kPilotPattern.size()];
            paint_cell(pixels, width, pitch, left_x + column,
                       first_cell_y + row, color);
            paint_cell(pixels, width, pitch, right_x + column,
                       first_cell_y + row, color);
        }
    }
}

void paint_calibration(std::vector<Rgba8>& pixels,
                       const std::uint32_t width,
                       const std::uint16_t pitch,
                       const SurfaceLayout& layout) {
    constexpr std::array<Rgba8, 5> kCalibrationPatches{
        kBlack, kRed, kGreen, kBlue, kWhite};
    const auto first_x = layout.calibration_band.x / pitch;
    const auto first_y = layout.calibration_band.y / pitch;
    const auto columns = layout.calibration_band.width / pitch;
    for (std::uint32_t patch = 0U; patch < kCalibrationPatches.size();
         ++patch) {
        const auto begin = (columns * patch) /
                           static_cast<std::uint32_t>(kCalibrationPatches.size());
        const auto end = (columns * (patch + 1U)) /
                         static_cast<std::uint32_t>(kCalibrationPatches.size());
        for (std::uint32_t row = 0U; row < kMp0CalibrationRows; ++row) {
            for (std::uint32_t column = begin; column < end; ++column) {
                paint_cell(pixels, width, pitch, first_x + column,
                           first_y + row, kCalibrationPatches[patch]);
            }
        }
    }
}

Status encode_control_cells(
    const std::array<std::byte, kFrameHeaderBytes>& encoded,
    std::array<Rgb8Cell, kMp0ControlCells>& cells) {
    std::size_t cell_index = 0U;
    for (const auto byte : encoded) {
        const auto value = std::to_integer<std::uint8_t>(byte);
        for (unsigned int symbol = 0U; symbol < 4U; ++symbol) {
            const auto shift = 6U - (symbol * 2U);
            const auto control_symbol =
                static_cast<std::uint8_t>((value >> shift) & 0x03U);
            cells[cell_index++] = Rgb8Cell{
                static_cast<std::uint8_t>(control_symbol == 1U ? 0xffU : 0U),
                static_cast<std::uint8_t>(control_symbol == 2U ? 0xffU : 0U),
                static_cast<std::uint8_t>(control_symbol == 3U ? 0xffU : 0U)};
        }
    }
    return cell_index == cells.size() ? Status::ok : Status::protocol;
}

}  // namespace

Status calculate_mp0_layout(const Mp0SurfaceConfig& config,
                            SurfaceLayout& layout) {
    if (config.width == 0U || config.height == 0U || config.cell_pitch == 0U ||
        !aspect_matches(config)) {
        return Status::invalid_argument;
    }

    const auto pixel_count = static_cast<std::uint64_t>(config.width) *
                             static_cast<std::uint64_t>(config.height);
    if (pixel_count > kMaxMp0SurfacePixels ||
        pixel_count > std::numeric_limits<std::size_t>::max()) {
        return Status::resource_limit;
    }

    const auto columns = config.width / config.cell_pitch;
    const auto rows = config.height / config.cell_pitch;
    if (columns <= 2U * kMp0SafeMarginCells ||
        rows <= 2U * kMp0SafeMarginCells) {
        return Status::resource_limit;
    }

    const auto safe_columns = columns - 2U * kMp0SafeMarginCells;
    const auto safe_rows = rows - 2U * kMp0SafeMarginCells;
    const auto minimum_columns = 2U * kMp0AnchorCells +
                                 2U * kMp0PilotColumns + 1U;
    const auto minimum_rows = kMp0ControlRows + kMp0CalibrationRows + 1U;
    if (safe_columns < minimum_columns || safe_rows < minimum_rows) {
        return Status::resource_limit;
    }

    const auto control_columns = safe_columns - 2U * kMp0AnchorCells;
    const auto control_capacity =
        static_cast<std::uint64_t>(control_columns) * kMp0ControlRows;
    if (control_capacity <
        kMp0ControlCells * kMp0ControlRepetitions) {
        return Status::resource_limit;
    }

    const auto payload_columns = safe_columns -
                                  2U * kMp0AnchorCells -
                                  2U * kMp0PilotColumns;
    const auto payload_rows = safe_rows - kMp0ControlRows -
                              kMp0CalibrationRows;
    const auto payload_capacity =
        static_cast<std::uint64_t>(payload_columns) * payload_rows;
    if (payload_capacity > std::numeric_limits<std::size_t>::max()) {
        return Status::resource_limit;
    }

    SurfaceLayout candidate;
    candidate.grid_columns = columns;
    candidate.grid_rows = rows;
    candidate.safe_area = PixelRect{
        kMp0SafeMarginCells * config.cell_pitch,
        kMp0SafeMarginCells * config.cell_pitch,
        safe_columns * config.cell_pitch,
        safe_rows * config.cell_pitch,
    };
    candidate.control_band = PixelRect{
        candidate.safe_area.x + kMp0AnchorCells * config.cell_pitch,
        candidate.safe_area.y,
        control_columns * config.cell_pitch,
        kMp0ControlRows * config.cell_pitch,
    };
    candidate.calibration_band = PixelRect{
        candidate.control_band.x,
        candidate.safe_area.y +
            (safe_rows - kMp0CalibrationRows) * config.cell_pitch,
        candidate.control_band.width,
        kMp0CalibrationRows * config.cell_pitch,
    };
    candidate.payload_area = PixelRect{
        candidate.control_band.x + kMp0PilotColumns * config.cell_pitch,
        candidate.control_band.y + kMp0ControlRows * config.cell_pitch,
        payload_columns * config.cell_pitch,
        payload_rows * config.cell_pitch,
    };
    candidate.payload_columns = payload_columns;
    candidate.payload_rows = payload_rows;
    candidate.payload_cell_capacity = static_cast<std::size_t>(payload_capacity);
    layout = candidate;
    return Status::ok;
}

Status render_mp0_surface(const Mp0SurfaceConfig& config,
                          const FrameHeader& header,
                          const std::span<const Rgb8Cell> payload_cells,
                          OpticalSurface& surface) {
    if (header.profile_id != config.profile_id ||
        header.frame_seq != config.frame_seq ||
        header.cell_pitch != config.cell_pitch) {
        return Status::invalid_argument;
    }
    for (const auto& cell : payload_cells) {
        if (!valid_rgb8_cell(cell)) {
            return Status::invalid_argument;
        }
    }

    SurfaceLayout layout;
    const auto layout_status = calculate_mp0_layout(config, layout);
    if (layout_status != Status::ok) {
        return layout_status;
    }
    if (payload_cells.size() > layout.payload_cell_capacity) {
        return Status::resource_limit;
    }

    std::array<std::byte, kFrameHeaderBytes> encoded_header{};
    const auto header_status = encode_frame_header(header, encoded_header);
    if (header_status != Status::ok) {
        return header_status;
    }

    std::array<Rgb8Cell, kMp0ControlCells> control_cells{};
    const auto control_status = encode_control_cells(encoded_header,
                                                     control_cells);
    if (control_status != Status::ok) {
        return control_status;
    }

    const auto pixel_count = static_cast<std::size_t>(config.width) *
                             static_cast<std::size_t>(config.height);
    try {
        std::vector<Rgba8> candidate_pixels(pixel_count, kBlack);
        const auto first_safe_x = layout.safe_area.x / config.cell_pitch;
        const auto first_safe_y = layout.safe_area.y / config.cell_pitch;
        const auto safe_columns = layout.safe_area.width / config.cell_pitch;
        const auto safe_rows = layout.safe_area.height / config.cell_pitch;

        paint_anchor(candidate_pixels, config.width, config.cell_pitch,
                     first_safe_x, first_safe_y, kRed);
        paint_anchor(candidate_pixels, config.width, config.cell_pitch,
                     first_safe_x + safe_columns - kMp0AnchorCells,
                     first_safe_y, kGreen);
        paint_anchor(candidate_pixels, config.width, config.cell_pitch,
                     first_safe_x, first_safe_y + safe_rows - kMp0AnchorCells,
                     kBlue);
        paint_anchor(candidate_pixels, config.width, config.cell_pitch,
                     first_safe_x + safe_columns - kMp0AnchorCells,
                     first_safe_y + safe_rows - kMp0AnchorCells, kWhite);

        const auto control_first_x = layout.control_band.x / config.cell_pitch;
        const auto control_first_y = layout.control_band.y / config.cell_pitch;
        const auto control_columns =
            layout.control_band.width / config.cell_pitch;
        const auto control_cell_count = static_cast<std::size_t>(
            control_columns * kMp0ControlRows);
        for (std::size_t index = 0U; index < control_cell_count; ++index) {
            const auto row = static_cast<std::uint32_t>(index /
                                                         control_columns);
            const auto column = static_cast<std::uint32_t>(index %
                                                            control_columns);
            paint_cell(candidate_pixels, config.width, config.cell_pitch,
                       control_first_x + column, control_first_y + row,
                       rgba_for_rgb8_cell(
                           control_cells[index % control_cells.size()]));
        }

        const auto payload_first_x = layout.payload_area.x / config.cell_pitch;
        const auto payload_first_y = layout.payload_area.y / config.cell_pitch;
        for (std::size_t index = 0U; index < payload_cells.size(); ++index) {
            const auto row = static_cast<std::uint32_t>(
                index / layout.payload_columns);
            const auto column = static_cast<std::uint32_t>(
                index % layout.payload_columns);
            paint_cell(candidate_pixels, config.width, config.cell_pitch,
                       payload_first_x + column, payload_first_y + row,
                       rgba_for_rgb8_cell(payload_cells[index]));
        }

        paint_pilots(candidate_pixels, config.width, config.cell_pitch, layout,
                     payload_first_y, layout.payload_rows);
        paint_calibration(candidate_pixels, config.width, config.cell_pitch,
                          layout);

        surface.width = config.width;
        surface.height = config.height;
        surface.pixels = std::move(candidate_pixels);
        surface.frame_seq = config.frame_seq;
        surface.hold_index = config.hold_index;
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

}  // namespace glyph::optical
