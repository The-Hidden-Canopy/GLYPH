#include "glyph/phy/rgb8.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

    std::vector<glyph::Rgb8Decision> decisions(cells.size());
    for (std::size_t index = 0U; index < cells.size(); ++index) {
        decisions[index].symbol = static_cast<std::uint8_t>(
            ((cells[index].red == 0xffU) ? 0x04U : 0U) |
            ((cells[index].green == 0xffU) ? 0x02U : 0U) |
            ((cells[index].blue == 0xffU) ? 0x01U : 0U));
        decisions[index].erasure = false;
    }
    std::vector<std::uint32_t> decision_erasures;
    assert(glyph::decode_rgb8_decisions(
               decisions, source.size(), decoded, decision_erasures) ==
           glyph::Status::ok);
    assert(decoded == source);
    assert(decision_erasures.empty());

    auto erased_decisions = decisions;
    erased_decisions[0U].erasure = true;
    erased_decisions[0U].symbol = 0x07U;
    assert(glyph::decode_rgb8_decisions(erased_decisions, source.size(),
                                        decoded, decision_erasures) ==
           glyph::Status::ok);
    assert(decision_erasures.size() == 1U);
    assert(decision_erasures[0U] == 0U);

    const auto decisions_before_failure = decoded;
    const auto erasures_before_failure = decision_erasures;
    auto invalid_decisions = decisions;
    invalid_decisions[0U].symbol = 0x08U;
    assert(glyph::decode_rgb8_decisions(invalid_decisions, source.size(),
                                        decoded, decision_erasures) ==
           glyph::Status::invalid_argument);
    assert(decoded == decisions_before_failure);
    assert(decision_erasures == erasures_before_failure);

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

    const glyph::Rgb8Observation black{0.10F, 0.05F, 0.02F};
    const glyph::Rgb8Observation red{0.80F, 0.15F, 0.10F};
    const glyph::Rgb8Observation green{0.18F, 0.70F, 0.14F};
    const glyph::Rgb8Observation blue{0.14F, 0.17F, 0.77F};
    glyph::Rgb8Calibration mixed_calibration;
    assert(glyph::fit_rgb8_calibration(black, red, green, blue, 0.15F,
                                       mixed_calibration) ==
           glyph::Status::ok);
    assert(mixed_calibration.use_mixing_matrix);
    assert(glyph::classify_rgb8_cell(red, mixed_calibration, decision) ==
           glyph::Status::ok);
    assert(decision.symbol == 0x04U);
    assert(!decision.erasure);
    assert(glyph::classify_rgb8_cell(green, mixed_calibration, decision) ==
           glyph::Status::ok);
    assert(decision.symbol == 0x02U);
    assert(!decision.erasure);
    assert(glyph::classify_rgb8_cell(blue, mixed_calibration, decision) ==
           glyph::Status::ok);
    assert(decision.symbol == 0x01U);
    assert(!decision.erasure);
    assert(glyph::classify_rgb8_cell({0.84F, 0.27F, 0.85F},
                                      mixed_calibration, decision) ==
           glyph::Status::ok);
    assert(decision.symbol == 0x05U);
    assert(!decision.erasure);

    const auto mixed_before_failure = mixed_calibration;
    assert(glyph::fit_rgb8_calibration(black, black, black, black, 0.15F,
                                       mixed_calibration) ==
           glyph::Status::invalid_argument);
    assert(mixed_calibration.use_mixing_matrix ==
           mixed_before_failure.use_mixing_matrix);

    const glyph::Rgb8Observation nearly_collinear_red{1.0F, 1.0F, 1.0F};
    const glyph::Rgb8Observation nearly_collinear_green{1.0F, 1.00001F, 1.0F};
    const glyph::Rgb8Observation nearly_collinear_blue{1.0F, 1.0F, 1.00001F};
    assert(glyph::fit_rgb8_calibration(
               {}, nearly_collinear_red, nearly_collinear_green,
               nearly_collinear_blue, 0.15F, mixed_calibration) ==
           glyph::Status::invalid_argument);
    assert(mixed_calibration.inverse_mixing == mixed_before_failure.inverse_mixing);

    assert(glyph::fit_rgb8_calibration(
               {NAN, 0.0F, 0.0F}, red, green, blue, 0.15F,
               mixed_calibration) == glyph::Status::invalid_argument);

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
