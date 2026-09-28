#include "glyph/frame/tile_payload.hpp"

#include "glyph/frame/crc32c.hpp"

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

    glyph::TilePayloadCodec codec;
    assert(glyph::TilePayloadCodec::create(config, codec) ==
           glyph::Status::ok);
    assert(codec.logical_payload_bytes() == 8U);
    assert(codec.wire_payload_bytes() == 12U);

    const std::array<std::byte, 5> logical_payload{
        std::byte{0x10}, std::byte{0x21}, std::byte{0x32}, std::byte{0x43},
        std::byte{0x54}};
    glyph::TileHeader header;
    header.tile_id = 7U;
    header.shard_index = 2U;
    header.fec_group = 11U;
    std::vector<std::byte> wire_payload;
    assert(codec.encode(header, logical_payload, wire_payload) ==
           glyph::Status::ok);
    assert(header.tile_id == 7U);
    assert(header.shard_index == 2U);
    assert(header.fec_group == 11U);
    assert(header.payload_len == logical_payload.size());
    assert(header.payload_crc32c == glyph::crc32c(logical_payload));
    assert(wire_payload.size() == codec.wire_payload_bytes());
    const std::array<std::uint8_t, 12> expected_wire{
        0x10U, 0x54U, 0x26U, 0x21U, 0x00U, 0xdbU,
        0x32U, 0x00U, 0xddU, 0x43U, 0x00U, 0x34U};
    for (std::size_t index = 0U; index < wire_payload.size(); ++index) {
        assert(std::to_integer<std::uint8_t>(wire_payload[index]) ==
               expected_wire[index]);
    }

    auto decoded = std::vector<std::byte>{std::byte{0x7f}};
    assert(codec.decode(header, wire_payload, decoded) == glyph::Status::ok);
    assert(decoded.size() == logical_payload.size());
    assert(std::equal(decoded.begin(), decoded.end(), logical_payload.begin()));

    auto corrected_wire = wire_payload;
    corrected_wire[4U] ^= std::byte{0x55};
    assert(codec.decode(header, corrected_wire, decoded) == glyph::Status::ok);
    assert(std::equal(decoded.begin(), decoded.end(), logical_payload.begin()));

    auto erased_wire = wire_payload;
    erased_wire[0U] = std::byte{0};
    const std::array<std::uint32_t, 1> wire_erasures{0U};
    assert(codec.decode(header, erased_wire, wire_erasures, decoded) ==
           glyph::Status::ok);
    assert(std::equal(decoded.begin(), decoded.end(), logical_payload.begin()));

    auto erased_with_present_corruption = erased_wire;
    erased_with_present_corruption[1U] ^= std::byte{0x7f};
    assert(codec.decode(header, erased_with_present_corruption, wire_erasures,
                        decoded) == glyph::Status::integrity);
    assert(std::equal(decoded.begin(), decoded.end(), logical_payload.begin()));

    const auto decoded_before_failure = decoded;
    auto bad_header = header;
    bad_header.payload_crc32c ^= 0x01U;
    assert(codec.decode(bad_header, wire_payload, decoded) ==
           glyph::Status::integrity);
    assert(decoded == decoded_before_failure);

    auto bad_length_header = header;
    bad_length_header.payload_len = 9U;
    assert(codec.decode(bad_length_header, wire_payload, decoded) ==
           glyph::Status::invalid_argument);
    assert(decoded == decoded_before_failure);

    auto short_wire = wire_payload;
    short_wire.pop_back();
    assert(codec.decode(header, short_wire, decoded) ==
           glyph::Status::invalid_argument);
    assert(decoded == decoded_before_failure);

    auto header_before_failure = header;
    const auto wire_before_failure = wire_payload;
    const std::array<std::byte, 9> oversized_payload{};
    assert(codec.encode(header, oversized_payload, wire_payload) ==
           glyph::Status::invalid_argument);
    assert(header.tile_id == header_before_failure.tile_id);
    assert(header.payload_len == header_before_failure.payload_len);
    assert(header.payload_crc32c == header_before_failure.payload_crc32c);
    assert(wire_payload == wire_before_failure);

    glyph::TilePayloadCodecConfig invalid = config;
    invalid.interleaver.max_bytes = 11U;
    assert(glyph::TilePayloadCodec::create(invalid, codec) ==
           glyph::Status::invalid_argument);

    return 0;
}
