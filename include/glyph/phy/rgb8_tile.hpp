#pragma once

#include "glyph/frame/tile_payload.hpp"
#include "glyph/phy/rgb8.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace glyph {

class Rgb8TileCodec final {
public:
    Rgb8TileCodec() = default;

    [[nodiscard]] static Status create(const TilePayloadCodecConfig& config,
                                       Rgb8TileCodec& codec);

    // Encodes a logical tile through tile CRC/ECC/interleaving and then the
    // strict RGB8 binary cell mapping. The header describes logical bytes.
    [[nodiscard]] Status encode(TileHeader& header,
                                std::span<const std::byte> logical_payload,
                                std::vector<Rgb8Cell>& cells) const;

    // Decodes strict binary RGB8 cells through tile deinterleaving/ECC/CRC.
    // Camera calibration and confidence-to-erasure decisions happen outside
    // this logical composition layer.
    [[nodiscard]] Status decode(const TileHeader& header,
                                std::span<const Rgb8Cell> cells,
                                std::vector<std::byte>& logical_payload) const;

    // Decodes classifier output and propagates erased-cell byte positions to
    // the tile inner-erasure path. Erased cells are never treated as guesses.
    [[nodiscard]] Status decode(
        const TileHeader& header,
        std::span<const Rgb8Decision> decisions,
        std::vector<std::byte>& logical_payload) const;

    [[nodiscard]] std::uint16_t logical_payload_bytes() const noexcept {
        return tile_.logical_payload_bytes();
    }

    [[nodiscard]] std::size_t wire_payload_bytes() const noexcept {
        return tile_.wire_payload_bytes();
    }

    [[nodiscard]] std::size_t cell_count() const noexcept {
        return (wire_payload_bytes() * 8U + 2U) / 3U;
    }

private:
    TilePayloadCodec tile_;
};

}  // namespace glyph
