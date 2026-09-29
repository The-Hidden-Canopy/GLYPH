#include "glyph/frame/frame.hpp"

#include "glyph/frame/crc32c.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

namespace glyph {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'G', 'L', 'Y', '1'};
constexpr std::size_t kHeaderCrcOffset = 60U;

void write_u16(std::array<std::byte, kFrameHeaderBytes>& bytes,
               const std::size_t offset,
               const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value >> 8U);
    bytes[offset + 1U] = static_cast<std::byte>(value);
}

void write_u32(std::array<std::byte, kFrameHeaderBytes>& bytes,
               const std::size_t offset,
               const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value >> 24U);
    bytes[offset + 1U] = static_cast<std::byte>(value >> 16U);
    bytes[offset + 2U] = static_cast<std::byte>(value >> 8U);
    bytes[offset + 3U] = static_cast<std::byte>(value);
}

void write_u64(std::array<std::byte, kFrameHeaderBytes>& bytes,
               const std::size_t offset,
               const std::uint64_t value) {
    for (unsigned int index = 0U; index < 8U; ++index) {
        bytes[offset + index] = static_cast<std::byte>(
            value >> (56U - (index * 8U)));
    }
}

void write_u16_tile(std::array<std::byte, kTileHeaderBytes>& bytes,
                    const std::size_t offset,
                    const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value >> 8U);
    bytes[offset + 1U] = static_cast<std::byte>(value);
}

void write_u32_tile(std::array<std::byte, kTileHeaderBytes>& bytes,
                    const std::size_t offset,
                    const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value >> 24U);
    bytes[offset + 1U] = static_cast<std::byte>(value >> 16U);
    bytes[offset + 2U] = static_cast<std::byte>(value >> 8U);
    bytes[offset + 3U] = static_cast<std::byte>(value);
}

std::uint16_t read_u16(const std::span<const std::byte> bytes,
                       const std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset]))
         << 8U) |
        std::to_integer<std::uint8_t>(bytes[offset + 1U]));
}

std::uint32_t read_u32(const std::span<const std::byte> bytes,
                       const std::size_t offset) {
    return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset]))
            << 24U) |
           (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[offset + 1U]))
            << 16U) |
           (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[offset + 2U]))
            << 8U) |
           static_cast<std::uint32_t>(
               std::to_integer<std::uint8_t>(bytes[offset + 3U]));
}

std::uint64_t read_u64(const std::span<const std::byte> bytes,
                       const std::size_t offset) {
    std::uint64_t value = 0U;
    for (unsigned int index = 0U; index < 8U; ++index) {
        value = (value << 8U) |
                std::to_integer<std::uint8_t>(bytes[offset + index]);
    }
    return value;
}

bool is_zero(const std::array<std::uint8_t, 16>& value) {
    return std::all_of(value.begin(), value.end(), [](const auto byte) {
        return byte == 0U;
    });
}

Status validate_frame_header(const FrameHeader& header,
                             const FrameDecodeLimits& limits) {
    if (header.version_major != 1U) {
        return Status::protocol;
    }
    if (header.header_len != kFrameHeaderBytes || header.reserved != 0U) {
        return Status::protocol;
    }
    if (is_zero(header.transfer_id)) {
        return Status::invalid_argument;
    }
    if (header.payload_bytes > limits.max_payload_bytes) {
        return Status::resource_limit;
    }
    if (header.cell_pitch > limits.max_cell_pitch) {
        return Status::resource_limit;
    }
    if ((header.tile_cols == 0U) != (header.tile_rows == 0U)) {
        return Status::protocol;
    }
    if (header.tile_cols != 0U && header.tile_rows != 0U) {
        const auto columns = static_cast<std::uint32_t>(header.tile_cols);
        const auto rows = static_cast<std::uint32_t>(header.tile_rows);
        if (columns > limits.max_tiles / rows) {
            return Status::resource_limit;
        }
    }
    return Status::ok;
}

}  // namespace

Status encode_frame_header(
    const FrameHeader& header,
    std::array<std::byte, kFrameHeaderBytes>& encoded) {
    const auto validation = validate_frame_header(header, {});
    if (validation != Status::ok) {
        return validation;
    }

    encoded.fill(std::byte{0});
    for (std::size_t index = 0U; index < kMagic.size(); ++index) {
        encoded[index] = static_cast<std::byte>(kMagic[index]);
    }
    encoded[4U] = static_cast<std::byte>(header.version_major);
    encoded[5U] = static_cast<std::byte>(header.version_minor);
    encoded[6U] = static_cast<std::byte>(header.frame_type);
    encoded[7U] = static_cast<std::byte>(header.profile_id);
    write_u16(encoded, 8U, header.flags);
    write_u16(encoded, 10U, header.header_len);
    for (std::size_t index = 0U; index < header.transfer_id.size(); ++index) {
        encoded[12U + index] = static_cast<std::byte>(header.transfer_id[index]);
    }
    write_u32(encoded, 28U, header.frame_seq);
    write_u32(encoded, 32U, header.block_id);
    write_u32(encoded, 36U, header.symbol_group_id);
    write_u32(encoded, 40U, header.payload_bytes);
    write_u16(encoded, 44U, header.tile_cols);
    write_u16(encoded, 46U, header.tile_rows);
    write_u16(encoded, 48U, header.cell_pitch);
    write_u16(encoded, 50U, header.reserved);
    write_u64(encoded, 52U, header.timestamp_ticks);
    write_u32(encoded, kHeaderCrcOffset,
              crc32c(std::span<const std::byte>(encoded.data(),
                                                 kHeaderCrcOffset)));
    return Status::ok;
}

Status decode_frame_header(const std::span<const std::byte> encoded,
                           FrameHeader& header,
                           const FrameDecodeLimits& limits) {
    if (encoded.size() < kFrameHeaderBytes) {
        return Status::invalid_argument;
    }
    for (std::size_t index = 0U; index < kMagic.size(); ++index) {
        if (std::to_integer<std::uint8_t>(encoded[index]) != kMagic[index]) {
            return Status::protocol;
        }
    }

    const auto expected_crc = read_u32(encoded, kHeaderCrcOffset);
    const auto actual_crc = crc32c(encoded.first(kHeaderCrcOffset));
    if (expected_crc != actual_crc) {
        return Status::integrity;
    }

    FrameHeader parsed;
    parsed.version_major = std::to_integer<std::uint8_t>(encoded[4U]);
    parsed.version_minor = std::to_integer<std::uint8_t>(encoded[5U]);
    parsed.frame_type = std::to_integer<std::uint8_t>(encoded[6U]);
    parsed.profile_id = std::to_integer<std::uint8_t>(encoded[7U]);
    parsed.flags = read_u16(encoded, 8U);
    parsed.header_len = read_u16(encoded, 10U);
    for (std::size_t index = 0U; index < parsed.transfer_id.size(); ++index) {
        parsed.transfer_id[index] =
            std::to_integer<std::uint8_t>(encoded[12U + index]);
    }
    parsed.frame_seq = read_u32(encoded, 28U);
    parsed.block_id = read_u32(encoded, 32U);
    parsed.symbol_group_id = read_u32(encoded, 36U);
    parsed.payload_bytes = read_u32(encoded, 40U);
    parsed.tile_cols = read_u16(encoded, 44U);
    parsed.tile_rows = read_u16(encoded, 46U);
    parsed.cell_pitch = read_u16(encoded, 48U);
    parsed.reserved = read_u16(encoded, 50U);
    parsed.timestamp_ticks = read_u64(encoded, 52U);
    parsed.header_crc32c = expected_crc;

    const auto validation = validate_frame_header(parsed, limits);
    if (validation != Status::ok) {
        return validation;
    }
    header = parsed;
    return Status::ok;
}

Status encode_tile_header(const TileHeader& header,
                          std::array<std::byte, kTileHeaderBytes>& encoded) {
    encoded.fill(std::byte{0});
    write_u16_tile(encoded, 0U, header.tile_id);
    write_u16_tile(encoded, 2U, header.shard_index);
    write_u16_tile(encoded, 4U, header.payload_len);
    write_u16_tile(encoded, 6U, header.fec_group);
    write_u32_tile(encoded, 8U, header.payload_crc32c);
    return Status::ok;
}

Status decode_tile_header(const std::span<const std::byte> encoded,
                          TileHeader& header) {
    if (encoded.size() < kTileHeaderBytes) {
        return Status::invalid_argument;
    }
    TileHeader parsed;
    parsed.tile_id = read_u16(encoded, 0U);
    parsed.shard_index = read_u16(encoded, 2U);
    parsed.payload_len = read_u16(encoded, 4U);
    parsed.fec_group = read_u16(encoded, 6U);
    parsed.payload_crc32c = read_u32(encoded, 8U);
    header = parsed;
    return Status::ok;
}

Status verify_tile_payload(const TileHeader& header,
                           const std::span<const std::byte> payload) {
    if (payload.size() != header.payload_len) {
        return Status::invalid_argument;
    }
    if (crc32c(payload) != header.payload_crc32c) {
        return Status::integrity;
    }
    return Status::ok;
}

}  // namespace glyph
