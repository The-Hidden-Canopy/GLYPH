#include "glyph/c_api.h"

#include "glyph/frame/frame.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

glyph_mp0_surface_config_t make_config() {
    return glyph_mp0_surface_config_t{
        sizeof(glyph_mp0_surface_config_t),
        GLYPH_C_ABI_VERSION,
        960U,
        540U,
        6U,
        GLYPH_SURFACE_LANDSCAPE_16_9,
        0U,
        0U,
        17U,
        2U,
    };
}

std::array<std::uint8_t, GLYPH_FRAME_HEADER_BYTES> make_header(
    const glyph_mp0_surface_config_t& config) {
    glyph::FrameHeader header;
    header.transfer_id[0] = 0x42U;
    header.frame_seq = config.frame_seq;
    header.payload_bytes = 12U;
    header.tile_cols = 2U;
    header.tile_rows = 2U;
    header.cell_pitch = config.cell_pitch;
    std::array<std::byte, glyph::kFrameHeaderBytes> encoded{};
    assert(glyph::encode_frame_header(header, encoded) == glyph::Status::ok);

    std::array<std::uint8_t, GLYPH_FRAME_HEADER_BYTES> result{};
    for (std::size_t index = 0U; index < result.size(); ++index) {
        result[index] = std::to_integer<std::uint8_t>(encoded[index]);
    }
    return result;
}

}  // namespace

bool same_pixel(const glyph_rgba8_t& left, const glyph_rgba8_t& right) {
    return left.red == right.red && left.green == right.green &&
           left.blue == right.blue && left.alpha == right.alpha;
}

bool same_pixels(const std::vector<glyph_rgba8_t>& left,
                 const std::vector<glyph_rgba8_t>& right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), same_pixel);
}

int main() {
    assert(glyph_c_abi_version() == GLYPH_C_ABI_VERSION);
    assert(glyph_status_name_c(GLYPH_STATUS_OK) != nullptr);

    auto config = make_config();
    glyph_mp0_renderer_t* renderer = nullptr;
    assert(glyph_mp0_renderer_create(&config, &renderer) == GLYPH_STATUS_OK);
    assert(renderer != nullptr);

    glyph_mp0_renderer_t* occupied_output = renderer;
    assert(glyph_mp0_renderer_create(&config, &occupied_output) ==
           GLYPH_STATUS_INVALID_ARGUMENT);
    assert(occupied_output == renderer);

    glyph_mp0_surface_layout_t layout{
        sizeof(glyph_mp0_surface_layout_t), GLYPH_C_ABI_VERSION};
    assert(glyph_mp0_renderer_get_layout(renderer, &layout) ==
           GLYPH_STATUS_OK);
    assert(layout.pixel_count == 960U * 540U);
    assert(layout.payload_cell_capacity > 4U);

    const auto encoded_header = make_header(config);
    const std::array<glyph_rgb8_cell_t, 4> payload{
        glyph_rgb8_cell_t{0xffU, 0U, 0U},
        glyph_rgb8_cell_t{0U, 0xffU, 0U},
        glyph_rgb8_cell_t{0U, 0U, 0xffU},
        glyph_rgb8_cell_t{0xffU, 0xffU, 0xffU},
    };
    std::vector<glyph_rgba8_t> pixels(
        static_cast<std::size_t>(layout.pixel_count),
        glyph_rgba8_t{0xa5U, 0xa5U, 0xa5U, 0xa5U});
    glyph_mp0_surface_buffer_t output{
        sizeof(glyph_mp0_surface_buffer_t),
        GLYPH_C_ABI_VERSION,
        pixels.data(),
        static_cast<std::uint64_t>(pixels.size()),
        0U,
        0U,
        0U,
        0U,
        0U,
    };
    assert(glyph_mp0_renderer_render(
               renderer, encoded_header.data(), encoded_header.size(),
               payload.data(), payload.size(), &output) == GLYPH_STATUS_OK);
    assert(output.pixel_count == layout.pixel_count);
    assert(output.width == config.width);
    assert(output.height == config.height);
    assert(output.frame_seq == config.frame_seq);
    const glyph_rgba8_t black_pixel{0U, 0U, 0U, 255U};
    const glyph_rgba8_t sentinel_pixel{0x5aU, 0x5aU, 0x5aU, 0x5aU};
    assert(same_pixel(pixels.front(), black_pixel));

    const auto pixels_after_success = pixels;
    const auto output_after_success = output;
    auto bad_header = encoded_header;
    bad_header.back() ^= 1U;
    assert(glyph_mp0_renderer_render(
               renderer, bad_header.data(), bad_header.size(), payload.data(),
               payload.size(), &output) == GLYPH_STATUS_INTEGRITY);
    assert(same_pixels(pixels, pixels_after_success));
    assert(output.pixel_count == output_after_success.pixel_count);
    assert(output.width == output_after_success.width);

    std::vector<glyph_rgba8_t> short_pixels(
        static_cast<std::size_t>(layout.pixel_count - 1U),
        glyph_rgba8_t{0x5aU, 0x5aU, 0x5aU, 0x5aU});
    auto short_output = output_after_success;
    short_output.pixels = short_pixels.data();
    short_output.pixel_capacity = short_pixels.size();
    assert(glyph_mp0_renderer_render(
               renderer, encoded_header.data(), encoded_header.size(),
               payload.data(), payload.size(), &short_output) ==
           GLYPH_STATUS_RESOURCE_LIMIT);
    assert(same_pixel(short_pixels.front(), sentinel_pixel));

    auto short_config = config;
    short_config.struct_size--;
    glyph_mp0_renderer_t* rejected_renderer = nullptr;
    assert(glyph_mp0_renderer_create(&short_config, &rejected_renderer) ==
           GLYPH_STATUS_INVALID_ARGUMENT);
    assert(rejected_renderer == nullptr);

    assert(glyph_mp0_renderer_set_frame(renderer, 18U, 4U) ==
           GLYPH_STATUS_OK);
    auto next_config = config;
    next_config.frame_seq = 18U;
    const auto next_header = make_header(next_config);
    assert(glyph_mp0_renderer_render(
               renderer, next_header.data(), next_header.size(), payload.data(),
               payload.size(), &output) == GLYPH_STATUS_OK);
    assert(output.frame_seq == 18U);
    assert(output.hold_index == 4U);

    glyph_mp0_renderer_destroy(renderer);
    return 0;
}
