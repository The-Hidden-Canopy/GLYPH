#include "glyph/fec/group_assembler.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>

namespace glyph {
namespace {

bool is_zero(const std::array<std::uint8_t, 16>& value) noexcept {
    return std::all_of(value.begin(), value.end(), [](const auto byte) {
        return byte == 0U;
    });
}

}  // namespace

Status FecGroupAssembler::create(const FecGroupAssemblerConfig& config,
                                 FecGroupAssembler& assembler) {
    if (is_zero(config.transfer_id) || config.block_bytes == 0U ||
        config.fec.data_shards == 0U || config.fec.parity_shards == 0U ||
        config.fec.shard_bytes == 0U ||
        config.fec.shard_bytes > std::numeric_limits<std::uint16_t>::max()) {
        return Status::invalid_argument;
    }
    const auto total_shards = static_cast<std::uint32_t>(
                                  config.fec.data_shards) +
                              config.fec.parity_shards;
    if (total_shards == 0U ||
        static_cast<std::uint64_t>(config.fec.data_shards) *
                config.fec.shard_bytes <
            config.block_bytes) {
        return Status::invalid_argument;
    }

    try {
        SystematicFec fec;
        const auto fec_status = SystematicFec::create(config.fec, fec);
        if (fec_status != Status::ok) {
            return fec_status;
        }

        FecGroupAssembler candidate;
        candidate.config_ = config;
        candidate.fec_ = std::move(fec);
        candidate.shards_.resize(total_shards);
        candidate.present_.assign(total_shards, false);
        candidate.received_.assign(total_shards, false);
        assembler = std::move(candidate);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status FecGroupAssembler::submit(const FrameHeader& frame_header,
                                 const TileHeader& tile_header,
                                 const std::span<const std::byte> shard,
                                 FecShardSubmitResult& result) {
    if (frame_header.transfer_id != config_.transfer_id ||
        frame_header.block_id != config_.block_id ||
        frame_header.symbol_group_id != config_.fec_group ||
        frame_header.payload_bytes != config_.fec.shard_bytes ||
        tile_header.fec_group != config_.fec_group ||
        tile_header.shard_index >=
            static_cast<std::uint16_t>(fec_.data_shards() +
                                       fec_.parity_shards()) ||
        tile_header.payload_len != config_.fec.shard_bytes ||
        shard.size() != config_.fec.shard_bytes) {
        return Status::protocol;
    }

    try {
        const auto index = static_cast<std::size_t>(tile_header.shard_index);
        if (received_[index]) {
            if (shards_[index] !=
                std::vector<std::byte>(shard.begin(), shard.end())) {
                return Status::integrity;
            }
            result = FecShardSubmitResult{false, complete_};
            return Status::ok;
        }

        if (present_[index] &&
            shards_[index] !=
                std::vector<std::byte>(shard.begin(), shard.end())) {
            return Status::integrity;
        }

        auto candidate_shards = shards_;
        auto candidate_present = present_;
        auto candidate_received = received_;
        candidate_shards[index].assign(shard.begin(), shard.end());
        candidate_present[index] = true;
        candidate_received[index] = true;

        const auto candidate_received_count = static_cast<std::uint16_t>(
            received_shards_ + (received_[index] ? 0U : 1U));
        std::vector<std::byte> candidate_block;
        bool candidate_complete = complete_;
        if (!candidate_complete &&
            candidate_received_count >= fec_.data_shards()) {
            const auto reconstruct_status =
                fec_.reconstruct(candidate_shards, candidate_present);
            if (reconstruct_status != Status::ok) {
                return reconstruct_status;
            }
            candidate_block.reserve(config_.block_bytes);
            std::uint64_t remaining = config_.block_bytes;
            for (std::size_t data_index = 0U;
                 data_index < fec_.data_shards() && remaining != 0U;
                 ++data_index) {
                const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
                    remaining, config_.fec.shard_bytes));
                candidate_block.insert(candidate_block.end(),
                                       candidate_shards[data_index].begin(),
                                       candidate_shards[data_index].begin() +
                                           static_cast<std::ptrdiff_t>(count));
                remaining -= count;
            }
            if (remaining != 0U || candidate_block.size() != config_.block_bytes) {
                return Status::integrity;
            }
            candidate_complete = true;
        }

        shards_ = std::move(candidate_shards);
        present_ = std::move(candidate_present);
        received_ = std::move(candidate_received);
        received_shards_ = candidate_received_count;
        if (!complete_ && candidate_complete) {
            block_ = std::move(candidate_block);
            complete_ = true;
        }
        result = FecShardSubmitResult{true, complete_};
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

}  // namespace glyph
