#include "glyph/fec/group_assembler.hpp"
#include "glyph/image/png.hpp"
#include "glyph/optical/frame_codec.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

int main() {
    using glyph::Status;

    const std::array<std::byte, 29> source{
        std::byte{0x00}, std::byte{0x11}, std::byte{0x22}, std::byte{0x33},
        std::byte{0x44}, std::byte{0x55}, std::byte{0x66}, std::byte{0x77},
        std::byte{0x88}, std::byte{0x99}, std::byte{0xaa}, std::byte{0xbb},
        std::byte{0xcc}, std::byte{0xdd}, std::byte{0xee}, std::byte{0xff},
        std::byte{0x10}, std::byte{0x21}, std::byte{0x32}, std::byte{0x43},
        std::byte{0x54}, std::byte{0x65}, std::byte{0x76}, std::byte{0x87},
        std::byte{0x98}, std::byte{0xa9}, std::byte{0xba}, std::byte{0xcb},
        std::byte{0xdc}};

    glyph::FecGroupAssemblerConfig assembler_config;
    assembler_config.transfer_id[0] = 0x7aU;
    assembler_config.block_id = 3U;
    assembler_config.fec_group = 9U;
    assembler_config.fec.data_shards = 4U;
    assembler_config.fec.parity_shards = 3U;
    assembler_config.fec.shard_bytes = 8U;
    assembler_config.block_bytes = source.size();

    glyph::SystematicFec fec;
    assert(glyph::SystematicFec::create(assembler_config.fec, fec) ==
           Status::ok);
    std::vector<std::vector<std::byte>> shards(assembler_config.fec.data_shards);
    for (std::size_t shard = 0U; shard < shards.size(); ++shard) {
        shards[shard].assign(assembler_config.fec.shard_bytes, std::byte{0});
        const auto source_offset = shard * assembler_config.fec.shard_bytes;
        const auto available =
            std::min<std::size_t>(assembler_config.fec.shard_bytes,
                                  source.size() - source_offset);
        std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(source_offset),
                    available, shards[shard].begin());
    }
    shards.resize(assembler_config.fec.data_shards +
                  assembler_config.fec.parity_shards);
    assert(fec.encode(shards) == Status::ok);

    glyph::optical::Mp0FrameCodecConfig frame_config;
    frame_config.surface.width = 960U;
    frame_config.surface.height = 540U;
    frame_config.surface.cell_pitch = 6U;
    frame_config.surface.frame_seq = 17U;
    frame_config.surface.hold_index = 0U;
    frame_config.tile.inner_rs.data_bytes = 8U;
    frame_config.tile.inner_rs.parity_bytes = 4U;
    frame_config.tile.interleaver.depth = 3U;
    frame_config.tile.interleaver.max_bytes = 64U;
    glyph::optical::Mp0FrameCodec frame_codec;
    assert(glyph::optical::Mp0FrameCodec::create(frame_config, frame_codec) ==
           Status::ok);

    glyph::FecGroupAssembler assembler;
    assert(glyph::FecGroupAssembler::create(assembler_config, assembler) ==
           Status::ok);

    const std::array<std::uint16_t, 4> receive_order{4U, 1U, 6U, 0U};
    for (const auto shard_index : receive_order) {
        glyph::TileHeader tile_metadata;
        tile_metadata.shard_index = shard_index;
        tile_metadata.fec_group = assembler_config.fec_group;
        glyph::optical::OpticalSurface rendered;
        assert(frame_codec.encode(assembler_config.transfer_id,
                                  assembler_config.block_id, tile_metadata,
                                  shards[shard_index], rendered) == Status::ok);

        std::vector<std::byte> encoded_png;
        assert(glyph::image::encode_rgba8_png(
                   rendered.width, rendered.height, rendered.pixels,
                   encoded_png) == Status::ok);
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        std::vector<glyph::optical::Rgba8> pixels;
        assert(glyph::image::decode_rgba8_png(encoded_png, width, height,
                                              pixels) == Status::ok);
        glyph::optical::OpticalSurface captured{
            width, height, std::move(pixels), rendered.frame_seq,
            rendered.hold_index};

        glyph::FrameHeader frame_header;
        glyph::TileHeader tile_header;
        std::vector<std::byte> recovered_shard;
        assert(frame_codec.decode(captured, frame_header, tile_header,
                                  recovered_shard) == Status::ok);
        assert(tile_header.shard_index == shard_index);
        assert(recovered_shard == shards[shard_index]);
        glyph::FecShardSubmitResult submit_result;
        assert(assembler.submit(frame_header, tile_header, recovered_shard,
                                submit_result) == Status::ok);
        assert(submit_result.accepted);
    }

    assert(assembler.complete());
    assert(std::vector<std::byte>(assembler.block().begin(),
                                 assembler.block().end()) ==
           std::vector<std::byte>(source.begin(), source.end()));
    return 0;
}
