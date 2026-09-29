#include "glyph/image/png.hpp"
#include "glyph/optical/decoder.hpp"
#include "glyph/optical/surface.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

glyph::optical::Mp0SurfaceConfig make_config() {
    glyph::optical::Mp0SurfaceConfig config;
    config.width = 960U;
    config.height = 540U;
    config.cell_pitch = 6U;
    config.frame_seq = 17U;
    config.hold_index = 2U;
    return config;
}

glyph::FrameHeader make_header(const glyph::optical::Mp0SurfaceConfig& config) {
    glyph::FrameHeader header;
    header.transfer_id[0] = 0x42U;
    header.frame_seq = config.frame_seq;
    header.payload_bytes = 12U;
    header.tile_cols = 2U;
    header.tile_rows = 2U;
    header.cell_pitch = config.cell_pitch;
    return header;
}

}  // namespace

int main() {
    using glyph::Status;
    const auto config = make_config();
    const auto header = make_header(config);
    const std::array<glyph::Rgb8Cell, 8> payload{
        glyph::Rgb8Cell{0xffU, 0U, 0U},
        glyph::Rgb8Cell{0U, 0xffU, 0U},
        glyph::Rgb8Cell{0U, 0U, 0xffU},
        glyph::Rgb8Cell{0xffU, 0xffU, 0xffU},
        glyph::Rgb8Cell{0U, 0U, 0U},
        glyph::Rgb8Cell{0xffU, 0U, 0xffU},
        glyph::Rgb8Cell{0xffU, 0xffU, 0U},
        glyph::Rgb8Cell{0U, 0xffU, 0xffU},
    };

    glyph::optical::OpticalSurface rendered;
    assert(glyph::optical::render_mp0_surface(config, header, payload, rendered) ==
           Status::ok);

    glyph::optical::SurfaceDecodeResult decoded;
    glyph::optical::SurfaceDecodeConfig decode_config;
    decode_config.cell_pitch = config.cell_pitch;
    decode_config.profile_id = config.profile_id;
    assert(glyph::optical::decode_mp0_surface(rendered, decode_config, decoded) ==
           Status::ok);
    assert(decoded.surface_found);
    assert(decoded.accepted);
    assert(decoded.geometry_confidence == 1.0F);
    assert(decoded.calibration_confidence == 1.0F);
    assert(decoded.logical_frame_seq == config.frame_seq);
    assert(decoded.control_repetitions_verified == 2U);
    assert(decoded.header.transfer_id == header.transfer_id);
    assert(decoded.payload_cells.size() > payload.size());
    for (std::size_t index = 0U; index < payload.size(); ++index) {
        assert(decoded.payload_cells[index] == payload[index]);
    }

    std::vector<std::byte> encoded_png;
    assert(glyph::image::encode_rgba8_png(rendered.width, rendered.height,
                                          rendered.pixels, encoded_png) ==
           Status::ok);
    std::uint32_t decoded_width = 0U;
    std::uint32_t decoded_height = 0U;
    std::vector<glyph::optical::Rgba8> decoded_pixels;
    assert(glyph::image::decode_rgba8_png(encoded_png, decoded_width,
                                          decoded_height, decoded_pixels) ==
           Status::ok);
    glyph::optical::OpticalSurface from_png{
        decoded_width, decoded_height, std::move(decoded_pixels),
        rendered.frame_seq, rendered.hold_index};
    glyph::optical::SurfaceDecodeResult from_png_result;
    assert(glyph::optical::decode_mp0_surface(from_png, decode_config,
                                              from_png_result) == Status::ok);
    assert(from_png_result.payload_cells == decoded.payload_cells);

    glyph::optical::SurfaceLayout layout;
    assert(glyph::optical::calculate_mp0_layout(config, layout) == Status::ok);
    auto bad_anchor = rendered;
    const auto anchor_x =
        (layout.safe_area.x / config.cell_pitch + 1U) * config.cell_pitch +
        config.cell_pitch / 2U;
    const auto anchor_y =
        (layout.safe_area.y / config.cell_pitch + 1U) * config.cell_pitch +
        config.cell_pitch / 2U;
    bad_anchor.pixels[static_cast<std::size_t>(anchor_y) * bad_anchor.width +
                     anchor_x] =
        glyph::optical::Rgba8{0U, 0U, 0U, 255U};
    const auto before_failure = decoded;
    assert(glyph::optical::decode_mp0_surface(bad_anchor, decode_config,
                                              decoded) == Status::integrity);
    assert(decoded.payload_cells == before_failure.payload_cells);
    assert(decoded.accepted == before_failure.accepted);

    auto bad_repetition = rendered;
    const auto repeat_index = glyph::optical::kMp0ControlCells;
    const auto repeat_columns = layout.control_band.width / config.cell_pitch;
    const auto repeat_x = layout.control_band.x / config.cell_pitch +
                          static_cast<std::uint32_t>(repeat_index %
                                                     repeat_columns);
    const auto repeat_y = layout.control_band.y / config.cell_pitch +
                          static_cast<std::uint32_t>(repeat_index /
                                                     repeat_columns);
    bad_repetition.pixels[static_cast<std::size_t>(repeat_y * config.cell_pitch +
                                                    config.cell_pitch / 2U) *
                              bad_repetition.width +
                          repeat_x * config.cell_pitch + config.cell_pitch / 2U] =
        glyph::optical::Rgba8{0U, 0U, 0U, 255U};
    assert(glyph::optical::decode_mp0_surface(bad_repetition, decode_config,
                                              decoded) == Status::integrity);
    assert(decoded.payload_cells == before_failure.payload_cells);

    return 0;
}
