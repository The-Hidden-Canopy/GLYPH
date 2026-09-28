#pragma once

#include "glyph/core/status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace glyph {

inline constexpr std::size_t kFrameHeaderBytes = 64U;
inline constexpr std::size_t kTileHeaderBytes = 12U;

struct FrameHeader {
    std::uint8_t version_major = 1U;
    std::uint8_t version_minor = 0U;
    std::uint8_t frame_type = 0U;
    std::uint8_t profile_id = 0U;
    std::uint16_t flags = 0U;
    std::uint16_t header_len = static_cast<std::uint16_t>(kFrameHeaderBytes);
    std::array<std::uint8_t, 16> transfer_id{};
    std::uint32_t frame_seq = 0U;
    std::uint32_t block_id = 0U;
    std::uint32_t symbol_group_id = 0U;
    std::uint32_t payload_bytes = 0U;
    std::uint16_t tile_cols = 0U;
    std::uint16_t tile_rows = 0U;
    std::uint16_t cell_pitch = 0U;
    std::uint16_t reserved = 0U;
    std::uint64_t timestamp_ticks = 0U;
    std::uint32_t header_crc32c = 0U;
};

struct TileHeader {
    std::uint16_t tile_id = 0U;
    std::uint16_t shard_index = 0U;
    std::uint16_t payload_len = 0U;
    std::uint16_t fec_group = 0U;
    std::uint32_t payload_crc32c = 0U;
};

struct FrameDecodeLimits {
    std::uint32_t max_payload_bytes = 16U << 20U;
    std::uint32_t max_tiles = 4096U;
    std::uint16_t max_cell_pitch = 4096U;
};

[[nodiscard]] Status encode_frame_header(
    const FrameHeader& header,
    std::array<std::byte, kFrameHeaderBytes>& encoded);

[[nodiscard]] Status decode_frame_header(
    std::span<const std::byte> encoded,
    FrameHeader& header,
    const FrameDecodeLimits& limits = {});

[[nodiscard]] Status encode_tile_header(
    const TileHeader& header,
    std::array<std::byte, kTileHeaderBytes>& encoded);

[[nodiscard]] Status decode_tile_header(
    std::span<const std::byte> encoded,
    TileHeader& header);

[[nodiscard]] Status verify_tile_payload(
    const TileHeader& header,
    std::span<const std::byte> payload);

}  // namespace glyph
