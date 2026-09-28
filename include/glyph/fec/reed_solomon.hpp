#pragma once

#include "glyph/core/status.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace glyph {

struct FecConfig {
    std::uint16_t data_shards = 32U;
    std::uint16_t parity_shards = 8U;
    std::uint32_t shard_bytes = 0U;
};

class SystematicFec final {
public:
    SystematicFec() = default;

    [[nodiscard]] static Status create(const FecConfig& config,
                                       SystematicFec& fec);

    [[nodiscard]] Status encode(
        std::vector<std::vector<std::byte>>& shards) const;

    [[nodiscard]] Status reconstruct(
        std::vector<std::vector<std::byte>>& shards,
        std::vector<bool>& present) const;

    [[nodiscard]] std::uint16_t data_shards() const noexcept {
        return data_shards_;
    }

    [[nodiscard]] std::uint16_t parity_shards() const noexcept {
        return parity_shards_;
    }

    [[nodiscard]] std::uint32_t shard_bytes() const noexcept {
        return shard_bytes_;
    }

private:
    std::uint16_t data_shards_ = 0U;
    std::uint16_t parity_shards_ = 0U;
    std::uint32_t shard_bytes_ = 0U;
    std::vector<std::vector<std::uint8_t>> generator_;
};

}  // namespace glyph
