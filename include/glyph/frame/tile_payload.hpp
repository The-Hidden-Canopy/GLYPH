#pragma once

#include "glyph/fec/inner_reed_solomon.hpp"
#include "glyph/frame/frame.hpp"
#include "glyph/frame/interleaver.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace glyph {

struct TilePayloadCodecConfig {
    InnerRsConfig inner_rs{};
    InterleaverConfig interleaver{};
};

class TilePayloadCodec final {
public:
    TilePayloadCodec() = default;

    [[nodiscard]] static Status create(const TilePayloadCodecConfig& config,
                                       TilePayloadCodec& codec);

    // The header describes the decoded logical payload. The emitted payload
    // is the fixed-size interleaved inner-RS codeword.
    [[nodiscard]] Status encode(TileHeader& header,
                                std::span<const std::byte> logical_payload,
                                std::vector<std::byte>& wire_payload) const;

    // Deinterleaves, corrects the inner codeword, strips deterministic zero
    // padding, and verifies the header CRC over the decoded logical payload.
    // On failure, logical_payload is left unchanged.
    [[nodiscard]] Status decode(const TileHeader& header,
                                std::span<const std::byte> wire_payload,
                                std::vector<std::byte>& logical_payload) const;

    // Same as decode(), but treats the supplied wire-byte positions as
    // unknown and recovers them through the inner erasure path.
    [[nodiscard]] Status decode(
        const TileHeader& header,
        std::span<const std::byte> wire_payload,
        std::span<const std::uint32_t> wire_erasure_positions,
        std::vector<std::byte>& logical_payload) const;

    [[nodiscard]] std::uint16_t logical_payload_bytes() const noexcept {
        return inner_.data_bytes();
    }

    [[nodiscard]] std::size_t wire_payload_bytes() const noexcept {
        return inner_.codeword_bytes();
    }

private:
    InnerReedSolomon inner_;
    BlockInterleaver interleaver_;
};

}  // namespace glyph
