#include "glyph/frame/interleaver.hpp"

#include <algorithm>
#include <cstddef>
#include <new>
#include <utility>

namespace glyph {
namespace {

Status transform(const std::span<const std::byte> input,
                 std::vector<std::byte>& output,
                 const std::uint16_t depth,
                 const std::uint32_t max_bytes,
                 const bool reverse) {
    if (depth == 0U || max_bytes == 0U) {
        return Status::invalid_argument;
    }
    if (input.size() > max_bytes) {
        return Status::resource_limit;
    }
    if (input.empty()) {
        output.clear();
        return Status::ok;
    }

    try {
        // Copy first so input and output may safely alias. The configured
        // bound keeps this temporary bounded even for an untrusted payload
        // length.
        const std::vector<std::byte> source(input.begin(), input.end());
        const auto rows = std::min<std::size_t>(source.size(), depth);
        const auto columns = (source.size() + rows - 1U) / rows;
        std::vector<std::byte> candidate(source.size(), std::byte{0});

        std::size_t output_index = 0U;
        for (std::size_t column = 0U; column < columns; ++column) {
            for (std::size_t row = 0U; row < rows; ++row) {
                const auto source_index = row * columns + column;
                if (source_index >= source.size()) {
                    continue;
                }
                if (reverse) {
                    candidate[source_index] = source[output_index];
                } else {
                    candidate[output_index] = source[source_index];
                }
                ++output_index;
            }
        }
        if (output_index != source.size()) {
            return Status::integrity;
        }
        output = std::move(candidate);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

}  // namespace

Status BlockInterleaver::create(const InterleaverConfig& config,
                                BlockInterleaver& interleaver) {
    if (config.depth == 0U || config.max_bytes == 0U) {
        return Status::invalid_argument;
    }
    if (config.max_bytes > kMaxInterleaverBytes) {
        return Status::resource_limit;
    }

    BlockInterleaver candidate;
    candidate.depth_ = config.depth;
    candidate.max_bytes_ = config.max_bytes;
    interleaver = candidate;
    return Status::ok;
}

Status BlockInterleaver::interleave(
    const std::span<const std::byte> input,
    std::vector<std::byte>& output) const {
    return transform(input, output, depth_, max_bytes_, false);
}

Status BlockInterleaver::deinterleave(
    const std::span<const std::byte> input,
    std::vector<std::byte>& output) const {
    return transform(input, output, depth_, max_bytes_, true);
}

Status BlockInterleaver::map_interleaved_positions(
    const std::size_t payload_bytes,
    const std::span<const std::uint32_t> interleaved_positions,
    std::vector<std::uint32_t>& source_positions) const {
    if (depth_ == 0U || max_bytes_ == 0U) {
        return Status::invalid_argument;
    }
    if (payload_bytes > max_bytes_) {
        return Status::resource_limit;
    }
    if (payload_bytes == 0U && !interleaved_positions.empty()) {
        return Status::invalid_argument;
    }

    try {
        std::vector<bool> selected(payload_bytes, false);
        for (const auto position : interleaved_positions) {
            if (position >= payload_bytes || selected[position]) {
                return Status::invalid_argument;
            }
            selected[position] = true;
        }

        const auto rows = std::min<std::size_t>(payload_bytes, depth_);
        const auto columns = payload_bytes == 0U
                                 ? 0U
                                 : (payload_bytes + rows - 1U) / rows;
        std::vector<std::uint32_t> candidate;
        candidate.reserve(interleaved_positions.size());
        std::size_t interleaved_index = 0U;
        for (std::size_t column = 0U; column < columns; ++column) {
            for (std::size_t row = 0U; row < rows; ++row) {
                const auto source_index = row * columns + column;
                if (source_index >= payload_bytes) {
                    continue;
                }
                if (selected[interleaved_index]) {
                    candidate.push_back(
                        static_cast<std::uint32_t>(source_index));
                }
                ++interleaved_index;
            }
        }
        if (interleaved_index != payload_bytes ||
            candidate.size() != interleaved_positions.size()) {
            return Status::integrity;
        }
        source_positions = std::move(candidate);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

}  // namespace glyph
