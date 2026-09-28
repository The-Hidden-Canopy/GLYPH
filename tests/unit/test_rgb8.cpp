#include "glyph/phy/rgb8.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

int main() {
    const std::vector<std::byte> source{
        std::byte{0xb2}, std::byte{0x4d}};
    std::vector<glyph::Rgb8Cell> cells;
    assert(glyph::encode_rgb8_payload(source, cells) == glyph::Status::ok);
    assert(cells.size() == 6U);
    assert((cells[0U] == glyph::Rgb8Cell{0xffU, 0U, 0xffU}));
    assert((cells[1U] == glyph::Rgb8Cell{0xffU, 0U, 0U}));

    std::vector<std::byte> decoded;
    assert(glyph::decode_rgb8_payload(cells, source.size(), decoded) ==
           glyph::Status::ok);
    assert(decoded == source);

    auto invalid_cell = cells;
    invalid_cell[0U].red = 127U;
    assert(glyph::decode_rgb8_payload(invalid_cell, source.size(), decoded) ==
           glyph::Status::integrity);

    auto nonzero_padding = cells;
    nonzero_padding.back().blue = 0xffU;
    assert(glyph::decode_rgb8_payload(nonzero_padding, source.size(), decoded) ==
           glyph::Status::integrity);
    assert(glyph::decode_rgb8_payload(
               std::span<const glyph::Rgb8Cell>(cells).first(5U), source.size(),
               decoded) == glyph::Status::invalid_argument);

    glyph::Rgb8Calibration calibration;
    glyph::Rgb8Decision decision;
    assert(glyph::classify_rgb8_cell({1.0F, 0.0F, 1.0F}, calibration,
                                      decision) == glyph::Status::ok);
    assert(decision.symbol == 0x05U);
    assert(!decision.erasure);
    assert(decision.confidence[0] == 1.0F);

    assert(glyph::classify_rgb8_cell({0.5F, 0.0F, 1.0F}, calibration,
                                      decision) == glyph::Status::ok);
    assert(decision.erasure);

    assert(glyph::classify_rgb8_cell(
               {NAN, 0.0F, 1.0F}, calibration, decision) == glyph::Status::ok);
    assert(decision.erasure);

    auto invalid_calibration = calibration;
    invalid_calibration.one.red = invalid_calibration.black.red;
    assert(glyph::classify_rgb8_cell({1.0F, 0.0F, 1.0F}, invalid_calibration,
                                      decision) ==
           glyph::Status::invalid_argument);

    std::vector<glyph::Rgb8Cell> too_large;
    const std::vector<std::byte> one_byte{std::byte{0x01}};
    assert(glyph::encode_rgb8_payload(one_byte, too_large, 0U) ==
           glyph::Status::resource_limit);

    return 0;
}
