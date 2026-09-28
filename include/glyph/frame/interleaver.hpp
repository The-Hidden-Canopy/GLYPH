#pragma once

#include "glyph/core/status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace glyph {

inline constexpr std::uint32_t kMaxInterleaverBytes = 16U << 20U;

struct InterleaverConfig {
    // Number of logical rows used by the rectangular block permutation.
    std::uint16_t depth = 1U;
    std::uint32_t max_bytes = kMaxInterleaverBytes;
};

class BlockInterleaver final {
public:
    BlockInterleaver() = default;

    [[nodiscard]] static Status create(const InterleaverConfig& config,
                                       BlockInterleaver& interleaver);

    // Writes row-major input as column-major output. The permutation is
    // deterministic and preserves length; depth is part of the profile.
    [[nodiscard]] Status interleave(std::span<const std::byte> input,
                                    std::vector<std::byte>& output) const;

    // Reverses interleave() using the same configured depth.
    [[nodiscard]] Status deinterleave(std::span<const std::byte> input,
                                      std::vector<std::byte>& output) const;

    // Maps byte positions in interleaved order back to source/codeword order.
    // Positions must be unique and within payload_bytes.
    [[nodiscard]] Status map_interleaved_positions(
        std::size_t payload_bytes,
        std::span<const std::uint32_t> interleaved_positions,
        std::vector<std::uint32_t>& source_positions) const;

    [[nodiscard]] std::uint16_t depth() const noexcept {
        return depth_;
    }

    [[nodiscard]] std::uint32_t max_bytes() const noexcept {
        return max_bytes_;
    }

private:
    std::uint16_t depth_ = 0U;
    std::uint32_t max_bytes_ = 0U;
};

}  // namespace glyph
