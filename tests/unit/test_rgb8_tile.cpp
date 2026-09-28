#include "glyph/phy/rgb8_tile.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

int main() {
    glyph::TilePayloadCodecConfig config;
    config.inner_rs.data_bytes = 8U;
    config.inner_rs.parity_bytes = 4U;
    config.interleaver.depth = 3U;
    config.interleaver.max_bytes = 64U;

    glyph::Rgb8TileCodec codec;
    assert(glyph::Rgb8TileCodec::create(config, codec) == glyph::Status::ok);
    assert(codec.logical_payload_bytes() == 8U);
    assert(codec.wire_payload_bytes() == 12U);
    assert(codec.cell_count() == 32U);

    const std::array<std::byte, 5> logical_payload{
        std::byte{0x10}, std::byte{0x21}, std::byte{0x32}, std::byte{0x43},
        std::byte{0x54}};
    glyph::TileHeader header;
    header.tile_id = 9U;
    header.shard_index = 3U;
    header.fec_group = 12U;
    std::vector<glyph::Rgb8Cell> cells;
    assert(codec.encode(header, logical_payload, cells) == glyph::Status::ok);
    assert(header.payload_len == logical_payload.size());
    assert(cells.size() == codec.cell_count());
    assert((cells.front() == glyph::Rgb8Cell{0U, 0U, 0U}));

    std::vector<std::byte> decoded{std::byte{0x7f}};
    assert(codec.decode(header, cells, decoded) == glyph::Status::ok);
    assert(decoded.size() == logical_payload.size());
    assert(std::equal(decoded.begin(), decoded.end(), logical_payload.begin()));

    std::vector<glyph::Rgb8Decision> decisions(cells.size());
    for (std::size_t index = 0U; index < cells.size(); ++index) {
        decisions[index].symbol = static_cast<std::uint8_t>(
            ((cells[index].red == 0xffU) ? 0x04U : 0U) |
            ((cells[index].green == 0xffU) ? 0x02U : 0U) |
            ((cells[index].blue == 0xffU) ? 0x01U : 0U));
        decisions[index].erasure = false;
    }
    auto erased_decisions = decisions;
    erased_decisions[0U].erasure = true;
    erased_decisions[0U].symbol = 0x07U;
    assert(codec.decode(header, erased_decisions, decoded) ==
           glyph::Status::ok);
    assert(std::equal(decoded.begin(), decoded.end(), logical_payload.begin()));

    const auto decoded_before_erasure_failure = decoded;
    auto too_many_erased_decisions = decisions;
    for (const auto cell : std::array<std::size_t, 5>{0U, 3U, 6U, 9U, 12U}) {
        too_many_erased_decisions[cell].erasure = true;
    }
    assert(codec.decode(header, too_many_erased_decisions, decoded) ==
           glyph::Status::integrity);
    assert(decoded == decoded_before_erasure_failure);

    auto corrected_cells = cells;
    corrected_cells[0U].red = 0xffU;
    assert(codec.decode(header, corrected_cells, decoded) == glyph::Status::ok);
    assert(std::equal(decoded.begin(), decoded.end(), logical_payload.begin()));

    const auto decoded_before_failure = decoded;
    auto malformed_cells = cells;
    malformed_cells[1U].green = 127U;
    assert(codec.decode(header, malformed_cells, decoded) ==
           glyph::Status::integrity);
    assert(decoded == decoded_before_failure);

    auto short_cells = cells;
    short_cells.pop_back();
    assert(codec.decode(header, short_cells, decoded) ==
           glyph::Status::invalid_argument);
    assert(decoded == decoded_before_failure);

    auto bad_header = header;
    bad_header.payload_crc32c ^= 1U;
    assert(codec.decode(bad_header, cells, decoded) == glyph::Status::integrity);
    assert(decoded == decoded_before_failure);

    auto header_before_failure = header;
    const auto cells_before_failure = cells;
    const std::array<std::byte, 9> oversized_payload{};
    assert(codec.encode(header, oversized_payload, cells) ==
           glyph::Status::invalid_argument);
    assert(header.payload_len == header_before_failure.payload_len);
    assert(header.payload_crc32c == header_before_failure.payload_crc32c);
    assert(cells == cells_before_failure);

    return 0;
}
