#include "glyph/optical/frame_codec.hpp"

#include "glyph/frame/frame.hpp"
#include "glyph/phy/rgb8.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>

namespace glyph::optical {

Status Mp0FrameCodec::create(const Mp0FrameCodecConfig& config,
                             Mp0FrameCodec& codec) {
    SurfaceLayout layout;
    const auto layout_status = calculate_mp0_layout(config.surface, layout);
    if (layout_status != Status::ok) {
        return layout_status;
    }

    Rgb8TileCodec tile;
    const auto tile_status = Rgb8TileCodec::create(config.tile, tile);
    if (tile_status != Status::ok) {
        return tile_status;
    }
    const auto required_cells = kMp0TileHeaderCells + tile.cell_count();
    if (required_cells > layout.payload_cell_capacity) {
        return Status::resource_limit;
    }

    Mp0FrameCodec candidate;
    candidate.config_ = config;
    candidate.tile_ = std::move(tile);
    codec = std::move(candidate);
    return Status::ok;
}

Status Mp0FrameCodec::encode(
    const std::array<std::uint8_t, 16>& transfer_id,
    const std::uint32_t block_id,
    const TileHeader& tile_metadata,
    const std::span<const std::byte> logical_payload,
    OpticalSurface& surface) const {
    if (logical_payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return Status::resource_limit;
    }

    try {
        TileHeader tile_header = tile_metadata;
        std::vector<Rgb8Cell> tile_cells;
        const auto tile_status =
            tile_.encode(tile_header, logical_payload, tile_cells);
        if (tile_status != Status::ok) {
            return tile_status;
        }

        std::array<std::byte, kTileHeaderBytes> encoded_tile_header{};
        const auto tile_header_status =
            encode_tile_header(tile_header, encoded_tile_header);
        if (tile_header_status != Status::ok) {
            return tile_header_status;
        }

        std::vector<Rgb8Cell> tile_header_cells;
        const auto header_cells_status = encode_rgb8_payload(
            std::span<const std::byte>(encoded_tile_header.data(),
                                       encoded_tile_header.size()),
            tile_header_cells, encoded_tile_header.size());
        if (header_cells_status != Status::ok ||
            tile_header_cells.size() != kMp0TileHeaderCells) {
            return header_cells_status == Status::ok ? Status::protocol
                                                       : header_cells_status;
        }

        std::vector<Rgb8Cell> payload_cells;
        payload_cells.reserve(tile_header_cells.size() + tile_cells.size());
        payload_cells.insert(payload_cells.end(), tile_header_cells.begin(),
                             tile_header_cells.end());
        payload_cells.insert(payload_cells.end(), tile_cells.begin(),
                             tile_cells.end());

        FrameHeader frame_header;
        frame_header.transfer_id = transfer_id;
        frame_header.frame_seq = config_.surface.frame_seq;
        frame_header.block_id = block_id;
        frame_header.symbol_group_id = tile_header.fec_group;
        frame_header.payload_bytes =
            static_cast<std::uint32_t>(logical_payload.size());
        frame_header.tile_cols = 1U;
        frame_header.tile_rows = 1U;
        frame_header.cell_pitch = config_.surface.cell_pitch;
        frame_header.profile_id = config_.surface.profile_id;

        OpticalSurface candidate_surface;
        const auto render_status = render_mp0_surface(
            config_.surface, frame_header, payload_cells, candidate_surface);
        if (render_status != Status::ok) {
            return render_status;
        }
        surface = std::move(candidate_surface);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status Mp0FrameCodec::decode(const OpticalSurface& surface,
                             FrameHeader& frame_header,
                             TileHeader& tile_header,
                             std::vector<std::byte>& logical_payload) const {
    if (surface.width != config_.surface.width ||
        surface.height != config_.surface.height ||
        surface.frame_seq != config_.surface.frame_seq ||
        surface.hold_index != config_.surface.hold_index) {
        return Status::protocol;
    }
    try {
        SurfaceDecodeConfig surface_config;
        surface_config.cell_pitch = config_.surface.cell_pitch;
        surface_config.aspect = config_.surface.aspect;
        surface_config.profile_id = config_.surface.profile_id;
        surface_config.max_payload_cells =
            config_.surface.width == 0U || config_.surface.height == 0U
                ? 0U
                : kMaxMp0SurfacePixels;

        SurfaceDecodeResult decoded_surface;
        const auto surface_status =
            decode_mp0_surface(surface, surface_config, decoded_surface);
        if (surface_status != Status::ok) {
            return surface_status;
        }
        if (decoded_surface.payload_cells_erased != 0U ||
            decoded_surface.header.tile_cols != 1U ||
            decoded_surface.header.tile_rows != 1U ||
            decoded_surface.payload_cells.size() < frame_payload_cell_count()) {
            return Status::protocol;
        }

        const auto payload_cells = std::span<const Rgb8Cell>(
            decoded_surface.payload_cells.data(), frame_payload_cell_count());
        std::vector<std::byte> encoded_tile_header;
        const auto header_status = decode_rgb8_payload(
            payload_cells.first(kMp0TileHeaderCells), kTileHeaderBytes,
            encoded_tile_header, kTileHeaderBytes);
        if (header_status != Status::ok) {
            return header_status;
        }

        TileHeader candidate_tile_header;
        const auto tile_header_status = decode_tile_header(
            encoded_tile_header, candidate_tile_header);
        if (tile_header_status != Status::ok) {
            return tile_header_status;
        }
        if (decoded_surface.header.payload_bytes !=
            candidate_tile_header.payload_len) {
            return Status::protocol;
        }

        std::vector<std::byte> candidate_payload;
        const auto tile_status = tile_.decode(
            candidate_tile_header,
            payload_cells.subspan(kMp0TileHeaderCells, tile_.cell_count()),
            candidate_payload);
        if (tile_status != Status::ok) {
            return tile_status;
        }

        frame_header = decoded_surface.header;
        tile_header = candidate_tile_header;
        logical_payload = std::move(candidate_payload);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

}  // namespace glyph::optical
