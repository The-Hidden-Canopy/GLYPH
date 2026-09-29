#include "glyph/frame/interleaver.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

std::vector<std::byte> make_bytes(const std::size_t count) {
    std::vector<std::byte> value(count);
    for (std::size_t index = 0U; index < count; ++index) {
        value[index] = static_cast<std::byte>(index);
    }
    return value;
}

}  // namespace

int main() {
    glyph::InterleaverConfig config;
    config.depth = 3U;
    config.max_bytes = 64U;

    glyph::BlockInterleaver interleaver;
    assert(glyph::BlockInterleaver::create(config, interleaver) ==
           glyph::Status::ok);
    assert(interleaver.depth() == 3U);
    assert(interleaver.max_bytes() == 64U);

    const auto input = make_bytes(10U);
    std::vector<std::byte> output;
    assert(interleaver.interleave(input, output) == glyph::Status::ok);
    const std::array<std::uint8_t, 10> expected{0U, 4U, 8U, 1U, 5U,
                                                9U, 2U, 6U, 3U, 7U};
    for (std::size_t index = 0U; index < output.size(); ++index) {
        assert(std::to_integer<std::uint8_t>(output[index]) == expected[index]);
    }

    std::vector<std::byte> round_trip;
    assert(interleaver.deinterleave(output, round_trip) == glyph::Status::ok);
    assert(round_trip == input);

    auto in_place = input;
    assert(interleaver.interleave(in_place, in_place) == glyph::Status::ok);
    assert(in_place == output);
    assert(interleaver.deinterleave(in_place, in_place) == glyph::Status::ok);
    assert(in_place == input);

    const std::array<std::uint32_t, 3> interleaved_positions{0U, 1U, 2U};
    std::vector<std::uint32_t> source_positions;
    assert(interleaver.map_interleaved_positions(
               input.size(), interleaved_positions, source_positions) ==
           glyph::Status::ok);
    const std::vector<std::uint32_t> expected_source_positions{0U, 4U, 8U};
    assert(source_positions == expected_source_positions);

    const std::array<std::uint32_t, 2> duplicate_positions{1U, 1U};
    const auto source_positions_before_failure = source_positions;
    assert(interleaver.map_interleaved_positions(
               input.size(), duplicate_positions, source_positions) ==
           glyph::Status::invalid_argument);
    assert(source_positions == source_positions_before_failure);

    for (std::uint16_t depth = 1U; depth <= 12U; ++depth) {
        glyph::InterleaverConfig sweep_config = config;
        sweep_config.depth = depth;
        glyph::BlockInterleaver sweep_interleaver;
        assert(glyph::BlockInterleaver::create(sweep_config,
                                               sweep_interleaver) ==
               glyph::Status::ok);
        for (std::size_t length = 1U; length <= 25U; ++length) {
            const auto sweep_input = make_bytes(length);
            std::vector<std::byte> sweep_output;
            std::vector<std::byte> sweep_round_trip;
            assert(sweep_interleaver.interleave(sweep_input, sweep_output) ==
                   glyph::Status::ok);
            assert(sweep_interleaver.deinterleave(sweep_output,
                                                  sweep_round_trip) ==
                   glyph::Status::ok);
            assert(sweep_round_trip == sweep_input);
        }
    }

    std::vector<std::byte> empty_output{std::byte{0x7f}};
    const std::vector<std::byte> empty_input;
    assert(interleaver.interleave(empty_input, empty_output) ==
           glyph::Status::ok);
    assert(empty_output.empty());

    glyph::InterleaverConfig wide = config;
    wide.depth = 64U;
    glyph::BlockInterleaver wide_interleaver;
    assert(glyph::BlockInterleaver::create(wide, wide_interleaver) ==
           glyph::Status::ok);
    const auto short_input = make_bytes(3U);
    assert(wide_interleaver.interleave(short_input, output) ==
           glyph::Status::ok);
    assert(output == short_input);

    glyph::InterleaverConfig bounded = config;
    bounded.max_bytes = 4U;
    glyph::BlockInterleaver bounded_interleaver;
    assert(glyph::BlockInterleaver::create(bounded, bounded_interleaver) ==
           glyph::Status::ok);
    const auto output_before_limit = output;
    const auto over_limit = make_bytes(5U);
    assert(bounded_interleaver.interleave(over_limit, output) ==
           glyph::Status::resource_limit);
    assert(output == output_before_limit);

    glyph::InterleaverConfig invalid = config;
    invalid.depth = 0U;
    assert(glyph::BlockInterleaver::create(invalid, interleaver) ==
           glyph::Status::invalid_argument);
    invalid = config;
    invalid.max_bytes = 0U;
    assert(glyph::BlockInterleaver::create(invalid, interleaver) ==
           glyph::Status::invalid_argument);
    invalid = config;
    invalid.max_bytes = glyph::kMaxInterleaverBytes + 1U;
    assert(glyph::BlockInterleaver::create(invalid, interleaver) ==
           glyph::Status::resource_limit);

    return 0;
}
