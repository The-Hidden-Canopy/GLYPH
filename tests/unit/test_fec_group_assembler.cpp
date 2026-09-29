#include "glyph/fec/group_assembler.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

glyph::FrameHeader frame_for(const std::array<std::uint8_t, 16>& transfer_id,
                             const std::uint32_t block_id,
                             const std::uint16_t fec_group,
                             const std::uint32_t shard_bytes) {
    glyph::FrameHeader header;
    header.transfer_id = transfer_id;
    header.block_id = block_id;
    header.symbol_group_id = fec_group;
    header.payload_bytes = shard_bytes;
    return header;
}

glyph::TileHeader tile_for(const std::uint16_t shard_index,
                           const std::uint16_t fec_group,
                           const std::uint32_t shard_bytes) {
    glyph::TileHeader header;
    header.shard_index = shard_index;
    header.fec_group = fec_group;
    header.payload_len = static_cast<std::uint16_t>(shard_bytes);
    return header;
}

}  // namespace

int main() {
    using glyph::Status;
    glyph::FecGroupAssemblerConfig config;
    config.transfer_id[0] = 0x61U;
    config.block_id = 12U;
    config.fec_group = 5U;
    config.fec.data_shards = 4U;
    config.fec.parity_shards = 3U;
    config.fec.shard_bytes = 8U;
    config.block_bytes = 29U;

    glyph::FecGroupAssembler assembler;
    assert(glyph::FecGroupAssembler::create(config, assembler) == Status::ok);
    assert(assembler.received_shards() == 0U);
    assert(!assembler.complete());

    const auto total = static_cast<std::size_t>(
        config.fec.data_shards + config.fec.parity_shards);
    std::vector<std::vector<std::byte>> shards(config.fec.data_shards);
    for (std::size_t shard = 0U; shard < shards.size(); ++shard) {
        shards[shard].resize(config.fec.shard_bytes);
        for (std::size_t byte = 0U; byte < shards[shard].size(); ++byte) {
            shards[shard][byte] = static_cast<std::byte>(
                (shard * 23U + byte * 7U + 9U) & 0xffU);
        }
    }
    const auto original_data = shards;
    shards.resize(total);
    glyph::SystematicFec fec;
    assert(glyph::SystematicFec::create(config.fec, fec) == Status::ok);
    assert(fec.encode(shards) == Status::ok);

    const auto frame = frame_for(config.transfer_id, config.block_id,
                                 config.fec_group, config.fec.shard_bytes);
    glyph::FecShardSubmitResult result{false, false};
    for (const auto shard_index : std::array<std::uint16_t, 4>{4U, 1U, 6U, 0U}) {
        auto tile = tile_for(shard_index, config.fec_group,
                             config.fec.shard_bytes);
        assert(assembler.submit(frame, tile, shards[shard_index], result) ==
               Status::ok);
        assert(result.accepted);
    }
    assert(assembler.complete());
    assert(assembler.received_shards() == 4U);
    const auto expected_block = std::vector<std::byte>(
        [&]() {
            std::vector<std::byte> value;
            for (const auto& data_shard : original_data) {
                value.insert(value.end(), data_shard.begin(), data_shard.end());
            }
            value.resize(config.block_bytes);
            return value;
        }());
    assert(std::vector<std::byte>(assembler.block().begin(),
                                 assembler.block().end()) == expected_block);

    auto duplicate_tile = tile_for(4U, config.fec_group,
                                   config.fec.shard_bytes);
    assert(assembler.submit(frame, duplicate_tile, shards[4U], result) ==
           Status::ok);
    assert(!result.accepted);
    assert(result.complete);

    auto late_tile = tile_for(2U, config.fec_group, config.fec.shard_bytes);
    const auto block_before_late = assembler.block();
    assert(assembler.submit(frame, late_tile, shards[2U], result) == Status::ok);
    assert(result.accepted);
    assert(result.complete);
    assert(std::equal(block_before_late.begin(), block_before_late.end(),
                     assembler.block().begin(), assembler.block().end()));

    auto conflicting = shards[2U];
    conflicting[0] ^= std::byte{0x01};
    assert(assembler.submit(frame, late_tile, conflicting, result) ==
           Status::integrity);
    assert(std::equal(block_before_late.begin(), block_before_late.end(),
                     assembler.block().begin(), assembler.block().end()));

    auto wrong_frame = frame;
    wrong_frame.block_id += 1U;
    assert(assembler.submit(wrong_frame, duplicate_tile, shards[4U], result) ==
           Status::protocol);

    glyph::FecGroupAssemblerConfig invalid = config;
    invalid.block_bytes = 33U;
    assert(glyph::FecGroupAssembler::create(invalid, assembler) ==
           Status::invalid_argument);
    invalid = config;
    invalid.transfer_id.fill(0U);
    assert(glyph::FecGroupAssembler::create(invalid, assembler) ==
           Status::invalid_argument);

    return 0;
}
