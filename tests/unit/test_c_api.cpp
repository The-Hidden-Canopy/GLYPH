#include "glyph/c_api.h"

#include "glyph/frame/frame.hpp"

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

glyph_mp0_surface_config_t make_config() {
    return glyph_mp0_surface_config_t{
        sizeof(glyph_mp0_surface_config_t),
        GLYPH_C_ABI_VERSION,
        960U,
        540U,
        6U,
        GLYPH_SURFACE_LANDSCAPE_16_9,
        0U,
        0U,
        17U,
        2U,
    };
}

std::array<std::uint8_t, GLYPH_FRAME_HEADER_BYTES> make_header(
    const glyph_mp0_surface_config_t& config) {
    glyph::FrameHeader header;
    header.transfer_id[0] = 0x42U;
    header.frame_seq = config.frame_seq;
    header.payload_bytes = 12U;
    header.tile_cols = 2U;
    header.tile_rows = 2U;
    header.cell_pitch = config.cell_pitch;
    std::array<std::byte, glyph::kFrameHeaderBytes> encoded{};
    assert(glyph::encode_frame_header(header, encoded) == glyph::Status::ok);

    std::array<std::uint8_t, GLYPH_FRAME_HEADER_BYTES> result{};
    for (std::size_t index = 0U; index < result.size(); ++index) {
        result[index] = std::to_integer<std::uint8_t>(encoded[index]);
    }
    return result;
}

std::filesystem::path make_session_root() {
    const auto base = std::filesystem::temp_directory_path();
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned int attempt = 0U; attempt < 16U; ++attempt) {
        const auto candidate =
            base / ("glyph-c-api-session-" + std::to_string(ticks) + "-" +
                    std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error) && !error) {
            return candidate;
        }
    }
    return {};
}

}  // namespace

bool same_pixel(const glyph_rgba8_t& left, const glyph_rgba8_t& right) {
    return left.red == right.red && left.green == right.green &&
           left.blue == right.blue && left.alpha == right.alpha;
}

bool same_pixels(const std::vector<glyph_rgba8_t>& left,
                 const std::vector<glyph_rgba8_t>& right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), same_pixel);
}

int main() {
    assert(glyph_c_abi_version() == GLYPH_C_ABI_VERSION);
    assert(glyph_status_name_c(GLYPH_STATUS_OK) != nullptr);

    auto config = make_config();
    glyph_mp0_renderer_t* renderer = nullptr;
    assert(glyph_mp0_renderer_create(&config, &renderer) == GLYPH_STATUS_OK);
    assert(renderer != nullptr);

    glyph_mp0_renderer_t* occupied_output = renderer;
    assert(glyph_mp0_renderer_create(&config, &occupied_output) ==
           GLYPH_STATUS_INVALID_ARGUMENT);
    assert(occupied_output == renderer);

    glyph_mp0_surface_layout_t layout{
        sizeof(glyph_mp0_surface_layout_t), GLYPH_C_ABI_VERSION};
    assert(glyph_mp0_renderer_get_layout(renderer, &layout) ==
           GLYPH_STATUS_OK);
    assert(layout.pixel_count == 960U * 540U);
    assert(layout.payload_cell_capacity > 4U);

    const auto encoded_header = make_header(config);
    const std::array<glyph_rgb8_cell_t, 4> payload{
        glyph_rgb8_cell_t{0xffU, 0U, 0U},
        glyph_rgb8_cell_t{0U, 0xffU, 0U},
        glyph_rgb8_cell_t{0U, 0U, 0xffU},
        glyph_rgb8_cell_t{0xffU, 0xffU, 0xffU},
    };
    std::vector<glyph_rgba8_t> pixels(
        static_cast<std::size_t>(layout.pixel_count),
        glyph_rgba8_t{0xa5U, 0xa5U, 0xa5U, 0xa5U});
    glyph_mp0_surface_buffer_t output{
        sizeof(glyph_mp0_surface_buffer_t),
        GLYPH_C_ABI_VERSION,
        pixels.data(),
        static_cast<std::uint64_t>(pixels.size()),
        0U,
        0U,
        0U,
        0U,
        0U,
    };
    assert(glyph_mp0_renderer_render(
               renderer, encoded_header.data(), encoded_header.size(),
               payload.data(), payload.size(), &output) == GLYPH_STATUS_OK);
    assert(output.pixel_count == layout.pixel_count);
    assert(output.width == config.width);
    assert(output.height == config.height);
    assert(output.frame_seq == config.frame_seq);
    const glyph_rgba8_t black_pixel{0U, 0U, 0U, 255U};
    const glyph_rgba8_t sentinel_pixel{0x5aU, 0x5aU, 0x5aU, 0x5aU};
    assert(same_pixel(pixels.front(), black_pixel));

    const auto pixels_after_success = pixels;
    const auto output_after_success = output;
    auto bad_header = encoded_header;
    bad_header.back() ^= 1U;
    assert(glyph_mp0_renderer_render(
               renderer, bad_header.data(), bad_header.size(), payload.data(),
               payload.size(), &output) == GLYPH_STATUS_INTEGRITY);
    assert(same_pixels(pixels, pixels_after_success));
    assert(output.pixel_count == output_after_success.pixel_count);
    assert(output.width == output_after_success.width);

    std::vector<glyph_rgba8_t> short_pixels(
        static_cast<std::size_t>(layout.pixel_count - 1U),
        glyph_rgba8_t{0x5aU, 0x5aU, 0x5aU, 0x5aU});
    auto short_output = output_after_success;
    short_output.pixels = short_pixels.data();
    short_output.pixel_capacity = short_pixels.size();
    assert(glyph_mp0_renderer_render(
               renderer, encoded_header.data(), encoded_header.size(),
               payload.data(), payload.size(), &short_output) ==
           GLYPH_STATUS_RESOURCE_LIMIT);
    assert(same_pixel(short_pixels.front(), sentinel_pixel));

    auto short_config = config;
    short_config.struct_size--;
    glyph_mp0_renderer_t* rejected_renderer = nullptr;
    assert(glyph_mp0_renderer_create(&short_config, &rejected_renderer) ==
           GLYPH_STATUS_INVALID_ARGUMENT);
    assert(rejected_renderer == nullptr);

    assert(glyph_mp0_renderer_set_frame(renderer, 18U, 4U) ==
           GLYPH_STATUS_OK);
    auto next_config = config;
    next_config.frame_seq = 18U;
    const auto next_header = make_header(next_config);
    assert(glyph_mp0_renderer_render(
               renderer, next_header.data(), next_header.size(), payload.data(),
               payload.size(), &output) == GLYPH_STATUS_OK);
    assert(output.frame_seq == 18U);
    assert(output.hold_index == 4U);

    glyph_mp0_renderer_destroy(renderer);

    const std::string session_object_text = "C ABI logical session object\n";
    const glyph_byte_span_t session_object{
        reinterpret_cast<const std::uint8_t*>(session_object_text.data()),
        session_object_text.size()};
    const std::string display_name = "c-api-session.bin";
    const std::string fec_profile = "RS32+8";
    glyph_sender_config_t sender_config{};
    sender_config.struct_size = sizeof(glyph_sender_config_t);
    sender_config.abi_version = GLYPH_C_ABI_VERSION;
    sender_config.transfer_id[0] = 0x21U;
    sender_config.block_size = 5U;
    sender_config.shard_size = 5U;
    sender_config.display_name =
        glyph_utf8_span_t{display_name.data(), display_name.size()};
    sender_config.fec_profile =
        glyph_utf8_span_t{fec_profile.data(), fec_profile.size()};
    sender_config.max_object_size = 1U << 20U;

    glyph_sender_t* sender = nullptr;
    assert(glyph_sender_create(&session_object, &sender_config, &sender) ==
           GLYPH_STATUS_OK);
    assert(glyph_sender_state(sender) == GLYPH_SENDER_IDLE);
    assert(glyph_sender_prepare(sender) == GLYPH_STATUS_OK);
    assert(glyph_sender_state(sender) == GLYPH_SENDER_MANIFEST_READY);
    assert(glyph_sender_begin_bootstrap(sender) == GLYPH_STATUS_OK);

    std::uint64_t manifest_bytes = 0U;
    assert(glyph_sender_get_manifest(sender, nullptr, 0U, &manifest_bytes) ==
           GLYPH_STATUS_RESOURCE_LIMIT);
    assert(manifest_bytes != 0U);
    std::vector<std::uint8_t> manifest(manifest_bytes);
    assert(glyph_sender_get_manifest(sender, manifest.data(), manifest.size(),
                                     &manifest_bytes) == GLYPH_STATUS_OK);

    const auto session_root = make_session_root();
    assert(!session_root.empty());
    const auto source_path = session_root / "c-api-source.bin";
    {
        std::ofstream source(source_path, std::ios::binary | std::ios::trunc);
        assert(source.is_open());
        source.write(session_object_text.data(),
                     static_cast<std::streamsize>(session_object_text.size()));
        assert(source.good());
    }
    const auto source_path_string = source_path.string();
    auto file_sender_config = sender_config;
    file_sender_config.transfer_id[0] = 0x22U;
    glyph_sender_t* file_sender = nullptr;
    const glyph_utf8_span_t source_path_span{
        source_path_string.data(), source_path_string.size()};
    assert(glyph_sender_open_file(&source_path_span, &file_sender_config,
                                  &file_sender) == GLYPH_STATUS_OK);
    assert(glyph_sender_state(file_sender) == GLYPH_SENDER_IDLE);
    assert(glyph_sender_prepare(file_sender) == GLYPH_STATUS_OK);
    assert(glyph_sender_state(file_sender) == GLYPH_SENDER_MANIFEST_READY);
    glyph_sender_destroy(file_sender);

    const auto session_root_string = session_root.string();
    const auto journal_path = session_root / "session.glj";
    const auto journal_path_string = journal_path.string();
    glyph_receiver_config_t receiver_config{};
    receiver_config.struct_size = sizeof(glyph_receiver_config_t);
    receiver_config.abi_version = GLYPH_C_ABI_VERSION;
    receiver_config.output_root = glyph_utf8_span_t{
        session_root_string.data(), session_root_string.size()};
    receiver_config.journal_path = glyph_utf8_span_t{
        journal_path_string.data(), journal_path_string.size()};
    receiver_config.supported_fec_profile =
        glyph_utf8_span_t{fec_profile.data(), fec_profile.size()};
    receiver_config.max_object_size = 1U << 20U;

    glyph_receiver_t* receiver = nullptr;
    assert(glyph_receiver_create(&receiver_config, &receiver) ==
           GLYPH_STATUS_OK);
    assert(glyph_receiver_state(receiver) == GLYPH_RECEIVER_IDLE);
    assert(glyph_receiver_begin_search(receiver) == GLYPH_STATUS_OK);
    assert(glyph_receiver_notify_surface_found(receiver) == GLYPH_STATUS_OK);
    assert(glyph_receiver_notify_calibrated(receiver) == GLYPH_STATUS_OK);
    const glyph_byte_span_t manifest_span{manifest.data(), manifest.size()};
    assert(glyph_receiver_submit_manifest(receiver, &manifest_span) ==
           GLYPH_STATUS_OK);

    std::vector<std::uint8_t> block(5U);
    bool paused = false;
    for (;;) {
        std::uint32_t block_id = 0U;
        std::uint64_t bytes_written = 0U;
        std::uint32_t available = 0U;
        assert(glyph_sender_next_block(
                   sender, &block_id, block.data(), block.size(),
                   &bytes_written, &available) == GLYPH_STATUS_OK);
        if (available == 0U) {
            break;
        }
        const glyph_byte_span_t block_span{block.data(), bytes_written};
        if (!paused) {
            assert(glyph_receiver_submit_block(receiver, block_id + 1U,
                                               &block_span) ==
                   GLYPH_STATUS_INVALID_ARGUMENT);
            assert(glyph_receiver_submit_block(receiver, block_id,
                                               &block_span) == GLYPH_STATUS_OK);
            assert(glyph_receiver_pause(receiver) == GLYPH_STATUS_OK);
            assert(glyph_receiver_state(receiver) == GLYPH_RECEIVER_PAUSED);
            glyph_receiver_destroy(receiver);
            receiver = nullptr;
            assert(glyph_receiver_create(&receiver_config, &receiver) ==
                   GLYPH_STATUS_OK);
            assert(glyph_receiver_begin_search(receiver) == GLYPH_STATUS_OK);
            assert(glyph_receiver_notify_surface_found(receiver) ==
                   GLYPH_STATUS_OK);
            assert(glyph_receiver_notify_calibrated(receiver) ==
                   GLYPH_STATUS_OK);
            assert(glyph_receiver_submit_manifest(receiver, &manifest_span) ==
                   GLYPH_STATUS_OK);
            paused = true;
        } else {
            assert(glyph_receiver_submit_block(receiver, block_id,
                                               &block_span) == GLYPH_STATUS_OK);
        }
    }
    assert(paused);
    assert(glyph_sender_state(sender) == GLYPH_SENDER_FINAL_REPEAT);
    assert(glyph_sender_complete_final_repeat(sender) == GLYPH_STATUS_OK);
    assert(glyph_sender_state(sender) == GLYPH_SENDER_DONE);
    std::uint64_t received_progress = 0U;
    assert(glyph_receiver_get_progress(receiver, &received_progress) ==
           GLYPH_STATUS_OK);
    assert(received_progress == session_object.size);
    assert(glyph_receiver_finalize(receiver) == GLYPH_STATUS_OK);
    assert(glyph_receiver_state(receiver) == GLYPH_RECEIVER_COMPLETE);

    std::uint64_t required_path_bytes = 0U;
    assert(glyph_receiver_get_final_path(receiver, nullptr, 0U,
                                         &required_path_bytes) ==
           GLYPH_STATUS_RESOURCE_LIMIT);
    assert(required_path_bytes > 1U);
    std::vector<char> final_path(required_path_bytes);
    assert(glyph_receiver_get_final_path(receiver, final_path.data(),
                                         final_path.size(),
                                         &required_path_bytes) ==
           GLYPH_STATUS_OK);
    std::ifstream received(final_path.data(), std::ios::binary);
    assert(received.is_open());
    const std::vector<char> received_bytes{
        std::istreambuf_iterator<char>(received), std::istreambuf_iterator<char>()};
    assert(received_bytes == std::vector<char>(session_object_text.begin(),
                                               session_object_text.end()));
    received.close();

    glyph_receiver_destroy(receiver);
    glyph_sender_destroy(sender);
    std::error_code session_cleanup_error;
    std::filesystem::remove_all(session_root, session_cleanup_error);
    assert(!session_cleanup_error);
    return 0;
}
