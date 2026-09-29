#pragma once

#include "glyph/fec/reed_solomon.hpp"
#include "glyph/frame/frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace glyph {

struct FecGroupAssemblerConfig {
    std::array<std::uint8_t, 16> transfer_id{};
    std::uint32_t block_id = 0U;
    std::uint16_t fec_group = 0U;
    FecConfig fec{};
    std::uint32_t block_bytes = 0U;
};

struct FecShardSubmitResult {
    bool accepted = false;
    bool complete = false;
};

class FecGroupAssembler final {
  public:
    FecGroupAssembler() = default;

    [[nodiscard]] static Status create(const FecGroupAssemblerConfig& config,
                                       FecGroupAssembler& assembler);

    // Accepts an already tile-CRC-verified logical shard. Shards may arrive
    // in any order. Exact duplicates are idempotent; conflicting duplicates
    // are integrity failures. The output result is transactional on failure.
    [[nodiscard]] Status submit(const FrameHeader& frame_header,
                                const TileHeader& tile_header,
                                std::span<const std::byte> shard,
                                FecShardSubmitResult& result);

    [[nodiscard]] bool complete() const noexcept { return complete_; }
    [[nodiscard]] std::uint16_t received_shards() const noexcept {
        return received_shards_;
    }
    [[nodiscard]] std::span<const std::byte> block() const noexcept {
        return block_;
    }

  private:
    FecGroupAssemblerConfig config_{};
    SystematicFec fec_{};
    std::vector<std::vector<std::byte>> shards_;
    std::vector<bool> present_;
    std::vector<bool> received_;
    std::vector<std::byte> block_;
    std::uint16_t received_shards_ = 0U;
    bool complete_ = false;
};

}  // namespace glyph
