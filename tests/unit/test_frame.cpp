#include "glyph/frame/crc32c.hpp"
#include "glyph/frame/frame.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace {

std::string bytes_hex(std::span<const std::byte> bytes) {
    constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(bytes.size() * 2U);
    for (const auto byte : bytes) {
        const auto value = static_cast<unsigned int>(byte);
        output.push_back(hex[(value >> 4U) & 0x0fU]);
        output.push_back(hex[value & 0x0fU]);
    }
    return output;
}

}  // namespace

int main() {
    const std::string_view known = "123456789";
    const auto known_bytes = std::as_bytes(
        std::span<const char>(known.data(), known.size()));
    assert(glyph::crc32c(known_bytes) == 0xe3069283U);

    glyph::FrameHeader source;
    source.version_major = 1U;
    source.version_minor = 0U;
    source.frame_type = 2U;
    source.profile_id = 7U;
    source.flags = 0x1234U;
    source.transfer_id = {0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U,
                          0x07U, 0x08U, 0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU,
                          0x0eU, 0x0fU};
    source.frame_seq = 0x01020304U;
    source.block_id = 0x05060708U;
    source.symbol_group_id = 0x090a0b0cU;
    source.payload_bytes = 0x00010203U;
    source.tile_cols = 8U;
    source.tile_rows = 4U;
    source.cell_pitch = 6U;
    source.timestamp_ticks = 0x0102030405060708ULL;

    std::array<std::byte, glyph::kFrameHeaderBytes> encoded{};
    assert(glyph::encode_frame_header(source, encoded) == glyph::Status::ok);
    assert(bytes_hex(encoded) ==
           "474c59310100020712340040000102030405060708090a0b0c0d0e0f"
           "0102030405060708090a0b0c0001020300080004000600000102030405060708"
           "54cc7ab7");

    glyph::FrameHeader decoded;
    assert(glyph::decode_frame_header(encoded, decoded) == glyph::Status::ok);
    assert(decoded.version_major == source.version_major);
    assert(decoded.frame_type == source.frame_type);
    assert(decoded.flags == source.flags);
    assert(decoded.transfer_id == source.transfer_id);
    assert(decoded.frame_seq == source.frame_seq);
    assert(decoded.payload_bytes == source.payload_bytes);
    assert(decoded.timestamp_ticks == source.timestamp_ticks);
    assert(decoded.header_crc32c == 0x54cc7ab7U);

    auto corrupted = encoded;
    corrupted[40U] ^= std::byte{0x01};
    assert(glyph::decode_frame_header(corrupted, decoded) ==
           glyph::Status::integrity);

    auto invalid_magic = encoded;
    invalid_magic[0U] = std::byte{'X'};
    assert(glyph::decode_frame_header(invalid_magic, decoded) ==
           glyph::Status::protocol);

    auto invalid_reserved = source;
    invalid_reserved.reserved = 1U;
    assert(glyph::encode_frame_header(invalid_reserved, encoded) ==
           glyph::Status::protocol);

    auto too_many_tiles = source;
    too_many_tiles.tile_cols = 255U;
    too_many_tiles.tile_rows = 255U;
    assert(glyph::encode_frame_header(too_many_tiles, encoded) ==
           glyph::Status::resource_limit);

    auto zero_transfer = source;
    zero_transfer.transfer_id.fill(0U);
    assert(glyph::encode_frame_header(zero_transfer, encoded) ==
           glyph::Status::invalid_argument);

    glyph::TileHeader tile;
    tile.tile_id = 0x0102U;
    tile.shard_index = 0x0304U;
    tile.payload_len = 0x0506U;
    tile.fec_group = 0x0708U;
    tile.payload_crc32c = 0x090a0b0cU;
    std::array<std::byte, glyph::kTileHeaderBytes> tile_encoded{};
    assert(glyph::encode_tile_header(tile, tile_encoded) == glyph::Status::ok);
    assert(bytes_hex(tile_encoded) == "0102030405060708090a0b0c");
    glyph::TileHeader decoded_tile;
    assert(glyph::decode_tile_header(tile_encoded, decoded_tile) ==
           glyph::Status::ok);
    assert(decoded_tile.tile_id == tile.tile_id);
    assert(decoded_tile.payload_crc32c == tile.payload_crc32c);

    const std::vector<std::byte> payload{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    tile.payload_len = static_cast<std::uint16_t>(payload.size());
    tile.payload_crc32c = glyph::crc32c(payload);
    assert(glyph::verify_tile_payload(tile, payload) == glyph::Status::ok);
    auto corrupted_payload = payload;
    corrupted_payload[1U] ^= std::byte{0x01};
    assert(glyph::verify_tile_payload(tile, corrupted_payload) ==
           glyph::Status::integrity);
    assert(glyph::verify_tile_payload(
               tile, std::span<const std::byte>(payload).first(2U)) ==
           glyph::Status::invalid_argument);

    return 0;
}
