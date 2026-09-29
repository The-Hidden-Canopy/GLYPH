#include "glyph/fec/group_assembler.hpp"
#include "glyph/fec/reed_solomon.hpp"
#include "glyph/image/png.hpp"
#include "glyph/optical/frame_codec.hpp"
#include "glyph/session/session.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::filesystem::path make_root() {
    const auto base = std::filesystem::temp_directory_path();
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned int attempt = 0U; attempt < 16U; ++attempt) {
        const auto candidate =
            base / ("glyph-synthetic-session-" + std::to_string(ticks) +
                    "-" + std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error) && !error) {
            return candidate;
        }
    }
    return {};
}

std::vector<std::byte> make_source() {
    std::vector<std::byte> source(67U);
    for (std::size_t index = 0U; index < source.size(); ++index) {
        source[index] = static_cast<std::byte>((index * 29U + 7U) & 0xffU);
    }
    return source;
}

glyph::Status transmit_block(
    const std::array<std::uint8_t, 16>& transfer_id,
    const glyph::session::LogicalBlock& block,
    glyph::session::ReceiverSession& receiver) {
    glyph::FecGroupAssemblerConfig assembler_config;
    assembler_config.transfer_id = transfer_id;
    assembler_config.block_id = block.block_id;
    assembler_config.fec_group = static_cast<std::uint16_t>(block.block_id);
    assembler_config.fec.data_shards = 4U;
    assembler_config.fec.parity_shards = 3U;
    assembler_config.fec.shard_bytes = 8U;
    assembler_config.block_bytes =
        static_cast<std::uint32_t>(block.bytes.size());

    glyph::SystematicFec fec;
    if (glyph::SystematicFec::create(assembler_config.fec, fec) !=
        glyph::Status::ok) {
        return glyph::Status::integrity;
    }

    std::vector<std::vector<std::byte>> shards(
        assembler_config.fec.data_shards);
    for (std::size_t shard = 0U; shard < shards.size(); ++shard) {
        shards[shard].assign(assembler_config.fec.shard_bytes, std::byte{0});
        const auto source_offset = shard * assembler_config.fec.shard_bytes;
        const auto remaining = block.bytes.size() > source_offset
                                   ? block.bytes.size() - source_offset
                                   : 0U;
        const auto available = std::min<std::size_t>(
            assembler_config.fec.shard_bytes, remaining);
        if (available != 0U) {
            std::copy_n(
                block.bytes.begin() + static_cast<std::ptrdiff_t>(source_offset),
                available, shards[shard].begin());
        }
    }
    shards.resize(assembler_config.fec.data_shards +
                  assembler_config.fec.parity_shards);
    if (fec.encode(shards) != glyph::Status::ok) {
        return glyph::Status::integrity;
    }

    glyph::FecGroupAssembler assembler;
    if (glyph::FecGroupAssembler::create(assembler_config, assembler) !=
        glyph::Status::ok) {
        return glyph::Status::integrity;
    }

    const std::array<std::uint16_t, 4> receive_order{4U, 1U, 6U, 0U};
    for (const auto shard_index : receive_order) {
        glyph::optical::Mp0FrameCodecConfig frame_config;
        frame_config.surface.width = 540U;
        frame_config.surface.height = 300U;
        frame_config.surface.cell_pitch = 6U;
        frame_config.surface.frame_seq =
            100U + block.block_id * 16U + shard_index;
        frame_config.surface.hold_index = 0U;
        frame_config.tile.inner_rs.data_bytes = 8U;
        frame_config.tile.inner_rs.parity_bytes = 4U;
        frame_config.tile.interleaver.depth = 3U;
        frame_config.tile.interleaver.max_bytes = 64U;

        glyph::optical::Mp0FrameCodec frame_codec;
        if (glyph::optical::Mp0FrameCodec::create(frame_config, frame_codec) !=
            glyph::Status::ok) {
            return glyph::Status::integrity;
        }

        glyph::TileHeader tile_metadata;
        tile_metadata.tile_id = shard_index;
        tile_metadata.shard_index = shard_index;
        tile_metadata.fec_group = assembler_config.fec_group;
        glyph::optical::OpticalSurface rendered;
        if (frame_codec.encode(transfer_id, block.block_id, tile_metadata,
                               shards[shard_index], rendered) !=
            glyph::Status::ok) {
            return glyph::Status::integrity;
        }

        std::vector<std::byte> encoded_png;
        if (glyph::image::encode_rgba8_png(
                rendered.width, rendered.height, rendered.pixels,
                encoded_png) != glyph::Status::ok) {
            return glyph::Status::integrity;
        }
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        std::vector<glyph::optical::Rgba8> pixels;
        if (glyph::image::decode_rgba8_png(encoded_png, width, height,
                                           pixels) != glyph::Status::ok) {
            return glyph::Status::integrity;
        }
        glyph::optical::OpticalSurface captured{
            width, height, std::move(pixels), rendered.frame_seq,
            rendered.hold_index};

        glyph::FrameHeader frame_header;
        glyph::TileHeader decoded_tile_header;
        std::vector<std::byte> recovered_shard;
        if (frame_codec.decode(captured, frame_header, decoded_tile_header,
                               recovered_shard) != glyph::Status::ok) {
            return glyph::Status::integrity;
        }
        glyph::FecShardSubmitResult submit_result;
        if (assembler.submit(frame_header, decoded_tile_header,
                             recovered_shard, submit_result) !=
            glyph::Status::ok) {
            return glyph::Status::integrity;
        }
    }

    if (!assembler.complete()) {
        return glyph::Status::integrity;
    }
    return receiver.submit_block(block.block_id, assembler.block());
}

}  // namespace

int main() {
    using glyph::Status;

    const auto root = make_root();
    assert(!root.empty());
    const auto source = make_source();

    glyph::session::SenderConfig sender_config;
    sender_config.transfer_id[0] = 0x82U;
    sender_config.display_name = "synthetic-session.bin";
    sender_config.block_size = 29U;
    sender_config.shard_size = 8U;
    sender_config.object_limits.max_object_size = 1U << 20U;

    glyph::session::SenderSession sender;
    assert(glyph::session::SenderSession::create(source, sender_config, sender) ==
           Status::ok);
    assert(sender.prepare() == Status::ok);
    assert(sender.begin_bootstrap() == Status::ok);

    glyph::session::ReceiverConfig receiver_config;
    receiver_config.output_root = root;
    receiver_config.journal_path = root / "synthetic-session.glj";
    receiver_config.object_limits.max_object_size = 1U << 20U;
    glyph::session::ReceiverSession receiver;
    assert(glyph::session::ReceiverSession::create(receiver_config, receiver) ==
           Status::ok);
    assert(receiver.begin_search() == Status::ok);
    assert(receiver.notify_surface_found() == Status::ok);
    assert(receiver.notify_calibrated() == Status::ok);
    assert(receiver.submit_manifest(sender.manifest_bytes()) == Status::ok);

    for (;;) {
        glyph::session::LogicalBlock block;
        bool available = false;
        assert(sender.next_block(block, available) == Status::ok);
        if (!available) {
            break;
        }
        assert(transmit_block(sender.manifest().transfer_id, block, receiver) ==
               Status::ok);
    }
    assert(sender.state() == glyph::session::SenderState::final_repeat);
    assert(sender.complete_final_repeat() == Status::ok);
    assert(receiver.bytes_received() == source.size());
    assert(receiver.finalize() == Status::ok);
    assert(receiver.state() == glyph::session::ReceiverState::complete);

    std::ifstream received(receiver.final_path(), std::ios::binary);
    assert(received.is_open());
    const std::vector<char> received_chars{
        std::istreambuf_iterator<char>(received), std::istreambuf_iterator<char>()};
    std::vector<std::byte> received_bytes;
    received_bytes.reserve(received_chars.size());
    for (const auto value : received_chars) {
        received_bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(value)));
    }
    assert(received_bytes == source);
    received.close();

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    assert(!cleanup_error);
    return 0;
}
