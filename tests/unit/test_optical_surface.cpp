#include "glyph/optical/surface.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

glyph::FrameHeader make_header(const glyph::optical::Mp0SurfaceConfig& config) {
    glyph::FrameHeader header;
    header.profile_id = config.profile_id;
    header.transfer_id[0] = 0x42U;
    header.frame_seq = config.frame_seq;
    header.block_id = 7U;
    header.symbol_group_id = 3U;
    header.payload_bytes = 12U;
    header.tile_cols = 2U;
    header.tile_rows = 2U;
    header.cell_pitch = config.cell_pitch;
    header.timestamp_ticks = 99U;
    return header;
}

glyph::optical::Rgba8 pixel_at(const glyph::optical::OpticalSurface& surface,
                               const std::uint32_t x,
                               const std::uint32_t y) {
    return surface.pixels[static_cast<std::size_t>(y) * surface.width + x];
}

}  // namespace

int main() {
    using glyph::Status;
    using glyph::optical::Mp0SurfaceConfig;
    using glyph::optical::OpticalSurface;
    using glyph::optical::SurfaceAspect;
    using glyph::optical::SurfaceLayout;

    Mp0SurfaceConfig config;
    config.width = 960U;
    config.height = 540U;
    config.cell_pitch = 6U;
    config.frame_seq = 17U;
    config.hold_index = 2U;

    SurfaceLayout layout;
    assert(glyph::optical::calculate_mp0_layout(config, layout) == Status::ok);
    assert(layout.safe_area.width % config.cell_pitch == 0U);
    assert(layout.control_band.width / config.cell_pitch *
               glyph::optical::kMp0ControlRows >=
           glyph::optical::kMp0ControlCells *
               glyph::optical::kMp0ControlRepetitions);
    assert(layout.payload_cell_capacity > 0U);
    assert(layout.payload_area.x > layout.control_band.x);

    const auto header = make_header(config);
    const std::array<glyph::Rgb8Cell, 4> payload{
        glyph::Rgb8Cell{0xffU, 0U, 0U},
        glyph::Rgb8Cell{0U, 0xffU, 0U},
        glyph::Rgb8Cell{0U, 0U, 0xffU},
        glyph::Rgb8Cell{0xffU, 0xffU, 0xffU},
    };

    OpticalSurface first;
    assert(glyph::optical::render_mp0_surface(config, header, payload, first) ==
           Status::ok);
    assert(first.width == config.width);
    assert(first.height == config.height);
    assert(first.frame_seq == config.frame_seq);
    assert(first.hold_index == config.hold_index);
    assert(first.pixels.size() ==
           static_cast<std::size_t>(config.width) * config.height);

    const glyph::optical::Rgba8 red{255U, 0U, 0U, 255U};
    const glyph::optical::Rgba8 black{0U, 0U, 0U, 255U};
    const glyph::optical::Rgba8 green{0U, 255U, 0U, 255U};
    const glyph::optical::Rgba8 blue{0U, 0U, 255U, 255U};
    const glyph::optical::Rgba8 white{255U, 255U, 255U, 255U};
    const auto safe_x = layout.safe_area.x;
    const auto safe_y = layout.safe_area.y;
    assert(pixel_at(first, safe_x + config.cell_pitch,
                    safe_y + config.cell_pitch) ==
           red);
    const auto control_x = layout.control_band.x;
    const auto control_y = layout.control_band.y;
    // The first header byte is 'G' (0x47): 01, 00, 01, 11.
    assert(pixel_at(first, control_x, control_y) == red);
    assert(pixel_at(first, control_x + config.cell_pitch, control_y) == black);
    assert(pixel_at(first, control_x + 2U * config.cell_pitch, control_y) ==
           red);
    assert(pixel_at(first, control_x + 3U * config.cell_pitch, control_y) ==
           blue);
    const auto control_columns = layout.control_band.width / config.cell_pitch;
    const auto repeated_index = glyph::optical::kMp0ControlCells;
    assert(pixel_at(first,
                    control_x +
                        static_cast<std::uint32_t>(repeated_index %
                                                   control_columns) *
                            config.cell_pitch,
                    control_y +
                        static_cast<std::uint32_t>(repeated_index /
                                                   control_columns) *
                            config.cell_pitch) ==
           red);
    assert(pixel_at(first, layout.payload_area.x, layout.payload_area.y) ==
           red);
    assert(pixel_at(first, layout.calibration_band.x,
                    layout.calibration_band.y) ==
           black);
    const auto calibration_columns =
        layout.calibration_band.width / config.cell_pitch;
    assert(pixel_at(first,
                    layout.calibration_band.x +
                        (2U * calibration_columns / 5U) *
                            config.cell_pitch,
                    layout.calibration_band.y) ==
           green);
    assert(pixel_at(first, layout.calibration_band.x +
                               layout.calibration_band.width - config.cell_pitch,
                    layout.calibration_band.y) ==
           white);

    OpticalSurface second;
    assert(glyph::optical::render_mp0_surface(config, header, payload, second) ==
           Status::ok);
    assert(first.pixels == second.pixels);

    auto changed_config = config;
    changed_config.frame_seq++;
    const auto changed_header = make_header(changed_config);
    OpticalSurface changed;
    assert(glyph::optical::render_mp0_surface(changed_config, changed_header,
                                              payload, changed) ==
           Status::ok);
    assert(first.pixels != changed.pixels);

    auto bad_header = header;
    bad_header.frame_seq++;
    OpticalSurface preserved = first;
    assert(glyph::optical::render_mp0_surface(config, bad_header, payload,
                                              preserved) ==
           Status::invalid_argument);
    assert(preserved.pixels == first.pixels);

    auto malformed_payload = payload;
    malformed_payload[0U].red = 127U;
    assert(glyph::optical::render_mp0_surface(config, header, malformed_payload,
                                              preserved) ==
           Status::invalid_argument);
    assert(preserved.pixels == first.pixels);

    std::vector<glyph::Rgb8Cell> oversized_payload(
        layout.payload_cell_capacity + 1U);
    assert(glyph::optical::render_mp0_surface(config, header, oversized_payload,
                                              preserved) ==
           Status::resource_limit);
    assert(preserved.pixels == first.pixels);

    auto small_config = config;
    small_config.width = 120U;
    small_config.height = 80U;
    assert(glyph::optical::calculate_mp0_layout(small_config, layout) ==
           Status::resource_limit);

    auto portrait_config = config;
    portrait_config.width = 540U;
    portrait_config.height = 960U;
    portrait_config.aspect = SurfaceAspect::portrait_9_16;
    assert(glyph::optical::calculate_mp0_layout(portrait_config, layout) ==
           Status::ok);
    portrait_config.aspect = SurfaceAspect::landscape_16_9;
    assert(glyph::optical::calculate_mp0_layout(portrait_config, layout) ==
           Status::invalid_argument);

    auto zero_id_header = header;
    zero_id_header.transfer_id.fill(0U);
    assert(glyph::optical::render_mp0_surface(config, zero_id_header, payload,
                                              preserved) ==
           Status::invalid_argument);
    assert(preserved.pixels == first.pixels);

    return 0;
}
