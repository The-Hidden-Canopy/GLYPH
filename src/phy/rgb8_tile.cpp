#include "glyph/phy/rgb8_tile.hpp"

#include <utility>

namespace glyph {

Status Rgb8TileCodec::create(const TilePayloadCodecConfig& config,
                             Rgb8TileCodec& codec) {
    TilePayloadCodec tile;
    const auto status = TilePayloadCodec::create(config, tile);
    if (status != Status::ok) {
        return status;
    }

    Rgb8TileCodec candidate;
    candidate.tile_ = std::move(tile);
    codec = std::move(candidate);
    return Status::ok;
}

Status Rgb8TileCodec::encode(
    TileHeader& header,
    const std::span<const std::byte> logical_payload,
    std::vector<Rgb8Cell>& cells) const {
    TileHeader candidate_header = header;
    std::vector<std::byte> wire_payload;
    const auto tile_status =
        tile_.encode(candidate_header, logical_payload, wire_payload);
    if (tile_status != Status::ok) {
        return tile_status;
    }

    std::vector<Rgb8Cell> candidate_cells;
    const auto rgb_status = encode_rgb8_payload(
        wire_payload, candidate_cells, wire_payload.size());
    if (rgb_status != Status::ok) {
        return rgb_status;
    }

    header = candidate_header;
    cells = std::move(candidate_cells);
    return Status::ok;
}

Status Rgb8TileCodec::decode(
    const TileHeader& header,
    const std::span<const Rgb8Cell> cells,
    std::vector<std::byte>& logical_payload) const {
    std::vector<std::byte> wire_payload;
    const auto rgb_status = decode_rgb8_payload(
        cells, wire_payload_bytes(), wire_payload, wire_payload_bytes());
    if (rgb_status != Status::ok) {
        return rgb_status;
    }
    return tile_.decode(header, wire_payload, logical_payload);
}

Status Rgb8TileCodec::decode(
    const TileHeader& header,
    const std::span<const Rgb8Decision> decisions,
    std::vector<std::byte>& logical_payload) const {
    std::vector<std::byte> wire_payload;
    std::vector<std::uint32_t> erasure_positions;
    const auto rgb_status = decode_rgb8_decisions(
        decisions, wire_payload_bytes(), wire_payload, erasure_positions,
        wire_payload_bytes());
    if (rgb_status != Status::ok) {
        return rgb_status;
    }
    return tile_.decode(header, wire_payload, erasure_positions,
                        logical_payload);
}

}  // namespace glyph
