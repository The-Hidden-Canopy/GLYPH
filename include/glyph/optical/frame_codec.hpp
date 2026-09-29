#pragma once

#include "glyph/optical/decoder.hpp"
#include "glyph/phy/rgb8_tile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace glyph::optical {

inline constexpr std::size_t kMp0TileHeaderCells =
    (kTileHeaderBytes * 8U + 2U) / 3U;

struct Mp0FrameCodecConfig {
    Mp0SurfaceConfig surface{};
    TilePayloadCodecConfig tile{};
};

class Mp0FrameCodec final {
  public:
    Mp0FrameCodec() = default;

    [[nodiscard]] static Status create(const Mp0FrameCodecConfig& config,
                                       Mp0FrameCodec& codec);

    // The MP0 saved-frame payload is a strict RGB8 encoding of one tile
    // miniheader followed by the tile codec's wire payload. This is a
    // deterministic software loopback seam, not a new camera-tolerant mode.
    [[nodiscard]] Status encode(
        const std::array<std::uint8_t, 16>& transfer_id,
        std::uint32_t block_id,
        const TileHeader& tile_metadata,
        std::span<const std::byte> logical_payload,
        OpticalSurface& surface) const;

    // Decodes one canonical saved surface and verifies the tile through the
    // existing inner ECC/interleaver/CRC boundary. The output is transactional.
    [[nodiscard]] Status decode(const OpticalSurface& surface,
                                FrameHeader& frame_header,
                                TileHeader& tile_header,
                                std::vector<std::byte>& logical_payload) const;

    [[nodiscard]] std::size_t frame_payload_cell_count() const noexcept {
        return kMp0TileHeaderCells + tile_.cell_count();
    }

  private:
    Mp0FrameCodecConfig config_{};
    Rgb8TileCodec tile_{};
};

}  // namespace glyph::optical
