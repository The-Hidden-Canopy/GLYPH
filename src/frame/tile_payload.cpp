#include "glyph/frame/tile_payload.hpp"

#include "glyph/frame/crc32c.hpp"

#include <algorithm>
#include <cstddef>
#include <new>
#include <utility>

namespace glyph {

Status TilePayloadCodec::create(const TilePayloadCodecConfig& config,
                                TilePayloadCodec& codec) {
    InnerReedSolomon inner;
    const auto inner_status = InnerReedSolomon::create(config.inner_rs, inner);
    if (inner_status != Status::ok) {
        return inner_status;
    }

    if (config.interleaver.max_bytes < inner.codeword_bytes()) {
        return Status::invalid_argument;
    }

    BlockInterleaver interleaver;
    const auto interleaver_status =
        BlockInterleaver::create(config.interleaver, interleaver);
    if (interleaver_status != Status::ok) {
        return interleaver_status;
    }

    TilePayloadCodec candidate;
    candidate.inner_ = std::move(inner);
    candidate.interleaver_ = std::move(interleaver);
    codec = std::move(candidate);
    return Status::ok;
}

Status TilePayloadCodec::encode(
    TileHeader& header,
    const std::span<const std::byte> logical_payload,
    std::vector<std::byte>& wire_payload) const {
    if (inner_.data_bytes() == 0U ||
        logical_payload.size() > logical_payload_bytes()) {
        return Status::invalid_argument;
    }

    try {
        std::vector<std::byte> padded(inner_.data_bytes(), std::byte{0});
        std::copy(logical_payload.begin(), logical_payload.end(), padded.begin());

        std::vector<std::byte> codeword;
        const auto encode_status = inner_.encode(padded, codeword);
        if (encode_status != Status::ok) {
            return encode_status;
        }

        std::vector<std::byte> candidate_wire;
        const auto interleave_status =
            interleaver_.interleave(codeword, candidate_wire);
        if (interleave_status != Status::ok) {
            return interleave_status;
        }

        TileHeader candidate_header = header;
        candidate_header.payload_len =
            static_cast<std::uint16_t>(logical_payload.size());
        candidate_header.payload_crc32c = crc32c(logical_payload);
        header = candidate_header;
        wire_payload = std::move(candidate_wire);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status TilePayloadCodec::decode(
    const TileHeader& header,
    const std::span<const std::byte> wire_payload,
    std::vector<std::byte>& logical_payload) const {
    return decode(header, wire_payload, std::span<const std::uint32_t>{},
                  logical_payload);
}

Status TilePayloadCodec::decode(
    const TileHeader& header,
    const std::span<const std::byte> wire_payload,
    const std::span<const std::uint32_t> wire_erasure_positions,
    std::vector<std::byte>& logical_payload) const {
    if (inner_.data_bytes() == 0U ||
        header.payload_len > logical_payload_bytes()) {
        return Status::invalid_argument;
    }
    if (wire_payload.size() != wire_payload_bytes()) {
        return Status::invalid_argument;
    }

    try {
        std::vector<std::byte> codeword;
        const auto deinterleave_status =
            interleaver_.deinterleave(wire_payload, codeword);
        if (deinterleave_status != Status::ok) {
            return deinterleave_status;
        }

        std::vector<std::uint32_t> codeword_erasure_positions;
        const auto map_status = interleaver_.map_interleaved_positions(
            codeword.size(), wire_erasure_positions, codeword_erasure_positions);
        if (map_status != Status::ok) {
            return map_status;
        }

        Status decode_status = Status::ok;
        if (codeword_erasure_positions.empty()) {
            decode_status = inner_.decode(codeword);
        } else {
            std::vector<std::uint16_t> bounded_positions;
            bounded_positions.reserve(codeword_erasure_positions.size());
            for (const auto position : codeword_erasure_positions) {
                if (position > 0xffffU) {
                    return Status::invalid_argument;
                }
                bounded_positions.push_back(static_cast<std::uint16_t>(position));
            }
            decode_status = inner_.recover_erasures(codeword, bounded_positions);
        }
        if (decode_status != Status::ok) {
            return decode_status;
        }

        std::vector<std::byte> candidate_logical(header.payload_len);
        std::copy_n(codeword.begin(), header.payload_len,
                    candidate_logical.begin());
        const auto verify_status = verify_tile_payload(header, candidate_logical);
        if (verify_status != Status::ok) {
            return verify_status;
        }

        logical_payload = std::move(candidate_logical);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

}  // namespace glyph
