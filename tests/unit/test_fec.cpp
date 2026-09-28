#include "glyph/fec/reed_solomon.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

int main() {
    glyph::FecConfig config;
    config.data_shards = 4U;
    config.parity_shards = 3U;
    config.shard_bytes = 32U;

    glyph::SystematicFec fec;
    assert(glyph::SystematicFec::create(config, fec) == glyph::Status::ok);
    assert(fec.data_shards() == config.data_shards);
    assert(fec.parity_shards() == config.parity_shards);
    assert(fec.shard_bytes() == config.shard_bytes);

    glyph::FecConfig baseline_config;
    baseline_config.shard_bytes = 64U;
    glyph::SystematicFec baseline_fec;
    assert(glyph::SystematicFec::create(baseline_config, baseline_fec) ==
           glyph::Status::ok);

    const auto total = static_cast<std::size_t>(config.data_shards +
                                                 config.parity_shards);
    std::vector<std::vector<std::byte>> shards(config.data_shards);
    for (std::size_t shard = 0U; shard < config.data_shards; ++shard) {
        shards[shard].resize(config.shard_bytes);
        for (std::size_t byte = 0U; byte < config.shard_bytes; ++byte) {
            shards[shard][byte] = static_cast<std::byte>(
                (shard * 37U + byte * 11U + 3U) & 0xffU);
        }
    }
    shards.resize(total);
    assert(fec.encode(shards) == glyph::Status::ok);
    const auto original = shards;

    for (const auto erased : std::array<std::array<std::size_t, 3>, 3>{
             std::array<std::size_t, 3>{0U, 2U, 4U},
             std::array<std::size_t, 3>{1U, 3U, 5U},
             std::array<std::size_t, 3>{2U, 4U, 6U}}) {
        shards = original;
        std::vector<bool> present(total, true);
        for (const auto index : erased) {
            present[index] = false;
            shards[index].clear();
        }
        assert(fec.reconstruct(shards, present) == glyph::Status::ok);
        assert(present == std::vector<bool>(total, true));
        assert(shards == original);
    }

    auto insufficient_shards = original;
    std::vector<bool> insufficient_present(total, false);
    insufficient_present[0U] = true;
    insufficient_present[1U] = true;
    insufficient_present[4U] = true;
    assert(fec.reconstruct(insufficient_shards, insufficient_present) ==
           glyph::Status::integrity);

    glyph::FecConfig invalid = config;
    invalid.data_shards = 0U;
    assert(glyph::SystematicFec::create(invalid, fec) ==
           glyph::Status::invalid_argument);
    invalid = config;
    invalid.data_shards = 250U;
    invalid.parity_shards = 6U;
    assert(glyph::SystematicFec::create(invalid, fec) ==
           glyph::Status::resource_limit);
    invalid = config;
    invalid.shard_bytes = 17U << 20U;
    assert(glyph::SystematicFec::create(invalid, fec) ==
           glyph::Status::resource_limit);

    return 0;
}
