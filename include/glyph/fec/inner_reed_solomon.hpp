#pragma once

#include "glyph/core/status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace glyph {

struct InnerRsConfig {
    std::uint16_t data_bytes = 223U;
    std::uint16_t parity_bytes = 32U;
};

class InnerReedSolomon final {
public:
    InnerReedSolomon() = default;

    [[nodiscard]] static Status create(const InnerRsConfig& config,
                                       InnerReedSolomon& code);

    [[nodiscard]] Status encode(std::span<const std::byte> data,
                                std::vector<std::byte>& codeword) const;

    // Corrects unknown byte errors within the guaranteed bound
    // 2 * errors <= parity_bytes. Outside that bound, a valid-codeword result
    // cannot prove that the original bytes were recovered; retain tile CRC
    // and whole-object SHA-256 verification at higher layers.
    // On failure, codeword is left unchanged.
    [[nodiscard]] Status decode(std::vector<std::byte>& codeword) const;

    // Recovers declared erasures when erasures <= parity_bytes. Present bytes
    // are checked against the reconstructed codeword before success.
    [[nodiscard]] Status recover_erasures(
        std::vector<std::byte>& codeword,
        std::span<const std::uint16_t> erasure_positions) const;

    [[nodiscard]] std::uint16_t data_bytes() const noexcept {
        return data_bytes_;
    }

    [[nodiscard]] std::uint16_t parity_bytes() const noexcept {
        return parity_bytes_;
    }

    [[nodiscard]] std::size_t codeword_bytes() const noexcept {
        return static_cast<std::size_t>(data_bytes_) + parity_bytes_;
    }

private:
    std::uint16_t data_bytes_ = 0U;
    std::uint16_t parity_bytes_ = 0U;
    std::vector<std::uint8_t> generator_polynomial_;
    std::vector<std::vector<std::uint8_t>> generator_matrix_;
};

}  // namespace glyph
