#include "glyph/image/png.hpp"
#include "glyph/optical/frame_codec.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

int main() {
    using glyph::Status;

    glyph::optical::Mp0FrameCodecConfig config;
    config.surface.width = 960U;
    config.surface.height = 540U;
    config.surface.cell_pitch = 6U;
    config.surface.frame_seq = 17U;
    config.surface.hold_index = 2U;
    config.tile.inner_rs.data_bytes = 8U;
    config.tile.inner_rs.parity_bytes = 4U;
    config.tile.interleaver.depth = 3U;
    config.tile.interleaver.max_bytes = 64U;

    glyph::optical::Mp0FrameCodec codec;
    assert(glyph::optical::Mp0FrameCodec::create(config, codec) == Status::ok);
    assert(codec.frame_payload_cell_count() == 64U);

    std::array<std::uint8_t, 16> transfer_id{};
    transfer_id[0] = 0x71U;
    const std::array<std::byte, 5> logical_payload{
        std::byte{0x10}, std::byte{0x21}, std::byte{0x32}, std::byte{0x43},
        std::byte{0x54}};
    glyph::TileHeader tile_metadata;
    tile_metadata.tile_id = 3U;
    tile_metadata.shard_index = 1U;
    tile_metadata.fec_group = 4U;

    glyph::optical::OpticalSurface surface;
    assert(codec.encode(transfer_id, 9U, tile_metadata, logical_payload,
                        surface) == Status::ok);

    glyph::FrameHeader frame_header;
    glyph::TileHeader tile_header;
    std::vector<std::byte> decoded_payload{std::byte{0x7f}};
    assert(codec.decode(surface, frame_header, tile_header, decoded_payload) ==
           Status::ok);
    assert(frame_header.transfer_id == transfer_id);
    assert(frame_header.block_id == 9U);
    assert(frame_header.payload_bytes == logical_payload.size());
    assert(tile_header.tile_id == tile_metadata.tile_id);
    assert(tile_header.shard_index == tile_metadata.shard_index);
    assert(tile_header.fec_group == tile_metadata.fec_group);
    assert(decoded_payload.size() == logical_payload.size());
    assert(decoded_payload ==
           std::vector<std::byte>(logical_payload.begin(), logical_payload.end()));

    std::vector<std::byte> encoded_png;
    assert(glyph::image::encode_rgba8_png(surface.width, surface.height,
                                          surface.pixels, encoded_png) ==
           Status::ok);
    std::uint32_t decoded_width = 0U;
    std::uint32_t decoded_height = 0U;
    std::vector<glyph::optical::Rgba8> decoded_pixels;
    assert(glyph::image::decode_rgba8_png(encoded_png, decoded_width,
                                          decoded_height, decoded_pixels) ==
           Status::ok);
    glyph::optical::OpticalSurface from_png{
        decoded_width, decoded_height, std::move(decoded_pixels),
        surface.frame_seq, surface.hold_index};
    glyph::FrameHeader png_frame_header;
    glyph::TileHeader png_tile_header;
    std::vector<std::byte> png_payload;
    assert(codec.decode(from_png, png_frame_header, png_tile_header,
                        png_payload) == Status::ok);
    assert(png_frame_header.transfer_id == transfer_id);
    assert(png_payload == decoded_payload);

    glyph::optical::SurfaceLayout layout;
    assert(glyph::optical::calculate_mp0_layout(config.surface, layout) ==
           Status::ok);
    auto bad_surface = surface;
    const auto control_x = layout.control_band.x + config.surface.cell_pitch / 2U;
    const auto control_y = layout.control_band.y + config.surface.cell_pitch / 2U;
    bad_surface.pixels[static_cast<std::size_t>(control_y) * bad_surface.width +
                       control_x] = glyph::optical::Rgba8{0U, 0U, 0U, 255U};
    const auto frame_before_failure = frame_header;
    const auto tile_before_failure = tile_header;
    const auto payload_before_failure = decoded_payload;
    assert(codec.decode(bad_surface, frame_header, tile_header,
                        decoded_payload) == Status::integrity);
    assert(frame_header.transfer_id == frame_before_failure.transfer_id);
    assert(tile_header.tile_id == tile_before_failure.tile_id);
    assert(decoded_payload == payload_before_failure);

    auto wrong_geometry = surface;
    wrong_geometry.hold_index += 1U;
    assert(codec.decode(wrong_geometry, frame_header, tile_header,
                        decoded_payload) == Status::protocol);

    return 0;
}
