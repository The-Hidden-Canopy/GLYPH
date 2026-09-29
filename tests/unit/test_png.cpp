#include "glyph/image/png.hpp"

#include "glyph/optical/surface.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

std::vector<glyph::optical::Rgba8> make_pixels() {
    std::vector<glyph::optical::Rgba8> pixels(7U * 5U);
    for (std::uint32_t row = 0U; row < 5U; ++row) {
        for (std::uint32_t column = 0U; column < 7U; ++column) {
            pixels[static_cast<std::size_t>(row) * 7U + column] =
                glyph::optical::Rgba8{
                    static_cast<std::uint8_t>(row * 31U + column * 7U),
                    static_cast<std::uint8_t>(255U - row * 19U - column * 5U),
                    static_cast<std::uint8_t>((row * 43U + column * 29U) &
                                              0xffU),
                    static_cast<std::uint8_t>(64U + row * 17U + column * 11U),
                };
        }
    }
    return pixels;
}

}  // namespace

int main() {
    using glyph::Status;
    const auto source = make_pixels();
    std::vector<std::byte> encoded{std::byte{0x5a}};
    assert(glyph::image::encode_rgba8_png(7U, 5U, source, encoded) ==
           Status::ok);
    assert(encoded.size() > 32U);
    assert(std::to_integer<std::uint8_t>(encoded[0U]) == 0x89U);
    assert(std::to_integer<std::uint8_t>(encoded[1U]) == 0x50U);

    std::uint32_t width = 99U;
    std::uint32_t height = 99U;
    std::vector<glyph::optical::Rgba8> decoded{
        glyph::optical::Rgba8{1U, 2U, 3U, 4U}};
    assert(glyph::image::decode_rgba8_png(encoded, width, height, decoded) ==
           Status::ok);
    assert(width == 7U);
    assert(height == 5U);
    assert(decoded == source);

    auto corrupted_crc = encoded;
    corrupted_crc[29U] ^= std::byte{1U};
    const auto old_width = width;
    const auto old_height = height;
    const auto old_pixels = decoded;
    assert(glyph::image::decode_rgba8_png(corrupted_crc, width, height,
                                          decoded) == Status::integrity);
    assert(width == old_width);
    assert(height == old_height);
    assert(decoded == old_pixels);

    auto truncated = encoded;
    truncated.pop_back();
    assert(glyph::image::decode_rgba8_png(truncated, width, height, decoded) !=
           Status::ok);
    assert(width == old_width);
    assert(height == old_height);
    assert(decoded == old_pixels);

    const std::array<glyph::optical::Rgba8, 1> one_pixel{
        glyph::optical::Rgba8{9U, 8U, 7U, 6U}};
    const auto encoded_before_failure = encoded;
    assert(glyph::image::encode_rgba8_png(2U, 2U, one_pixel, encoded) ==
           Status::invalid_argument);
    assert(encoded == encoded_before_failure);

    auto oversized = encoded;
    oversized[16U] = std::byte{0xffU};
    oversized[17U] = std::byte{0xffU};
    oversized[18U] = std::byte{0xffU};
    oversized[19U] = std::byte{0xffU};
    assert(glyph::image::decode_rgba8_png(oversized, width, height, decoded) !=
           Status::ok);
    assert(width == old_width);
    assert(height == old_height);
    assert(decoded == old_pixels);

    return 0;
}
