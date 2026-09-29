#include "glyph/c_api.h"

#include "glyph/core/status.hpp"
#include "glyph/frame/frame.hpp"
#include "glyph/optical/surface.hpp"
#include "glyph/session/session.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <vector>

struct glyph_mp0_renderer {
    glyph::optical::Mp0SurfaceConfig config{};
    glyph::optical::SurfaceLayout layout{};
};

struct glyph_sender {
    glyph::session::SenderSession session{};
    std::uint64_t block_size = 0U;
};

struct glyph_receiver {
    glyph::session::ReceiverSession session{};
};

namespace {

constexpr std::uint32_t kCAbiVersion = GLYPH_C_ABI_VERSION;

glyph_status_t to_c_status(const glyph::Status status) noexcept {
    switch (status) {
        case glyph::Status::ok:
            return GLYPH_STATUS_OK;
        case glyph::Status::invalid_argument:
            return GLYPH_STATUS_INVALID_ARGUMENT;
        case glyph::Status::io:
            return GLYPH_STATUS_IO;
        case glyph::Status::protocol:
            return GLYPH_STATUS_PROTOCOL;
        case glyph::Status::integrity:
            return GLYPH_STATUS_INTEGRITY;
        case glyph::Status::crypto:
            return GLYPH_STATUS_CRYPTO;
        case glyph::Status::camera:
            return GLYPH_STATUS_CAMERA;
        case glyph::Status::display:
            return GLYPH_STATUS_DISPLAY;
        case glyph::Status::resource_limit:
            return GLYPH_STATUS_RESOURCE_LIMIT;
        case glyph::Status::unsupported:
            return GLYPH_STATUS_UNSUPPORTED;
    }
    return GLYPH_STATUS_UNSUPPORTED;
}

bool valid_input_header(const std::uint32_t struct_size,
                        const std::uint32_t abi_version,
                        const std::size_t current_size) noexcept {
    return struct_size >= current_size && abi_version == kCAbiVersion;
}

glyph_status_t import_config(
    const glyph_mp0_surface_config_t& input,
    glyph::optical::Mp0SurfaceConfig& output) noexcept {
    if (!valid_input_header(input.struct_size, input.abi_version,
                            sizeof(glyph_mp0_surface_config_t)) ||
        input.reserved != 0U ||
        input.aspect > GLYPH_SURFACE_SQUARE) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }

    output.width = input.width;
    output.height = input.height;
    output.cell_pitch = input.cell_pitch;
    output.aspect = static_cast<glyph::optical::SurfaceAspect>(input.aspect);
    output.profile_id = input.profile_id;
    output.frame_seq = input.frame_seq;
    output.hold_index = input.hold_index;
    return GLYPH_STATUS_OK;
}

glyph_mp0_surface_rect_t export_rect(
    const glyph::optical::PixelRect& input) noexcept {
    return glyph_mp0_surface_rect_t{input.x, input.y, input.width,
                                    input.height};
}

void export_layout(const glyph::optical::Mp0SurfaceConfig& config,
                   const glyph::optical::SurfaceLayout& input,
                   glyph_mp0_surface_layout_t& output) noexcept {
    output.struct_size = sizeof(glyph_mp0_surface_layout_t);
    output.abi_version = kCAbiVersion;
    output.safe_area = export_rect(input.safe_area);
    output.control_band = export_rect(input.control_band);
    output.payload_area = export_rect(input.payload_area);
    output.calibration_band = export_rect(input.calibration_band);
    output.grid_columns = input.grid_columns;
    output.grid_rows = input.grid_rows;
    output.payload_columns = input.payload_columns;
    output.payload_rows = input.payload_rows;
    output.payload_cell_capacity =
        static_cast<std::uint64_t>(input.payload_cell_capacity);
    output.pixel_count = static_cast<std::uint64_t>(config.width) *
                         static_cast<std::uint64_t>(config.height);
}

bool valid_layout_output(const glyph_mp0_surface_layout_t& output) noexcept {
    return valid_input_header(output.struct_size, output.abi_version,
                              sizeof(glyph_mp0_surface_layout_t));
}

bool valid_buffer_output(const glyph_mp0_surface_buffer_t& output) noexcept {
    return valid_input_header(output.struct_size, output.abi_version,
                              sizeof(glyph_mp0_surface_buffer_t));
}

glyph_status_t import_utf8_span(const glyph_utf8_span_t input,
                                const bool required,
                                std::string& output) {
    if (input.size > std::numeric_limits<std::size_t>::max() ||
        input.size > std::numeric_limits<std::ptrdiff_t>::max() ||
        (input.size != 0U && input.data == nullptr)) {
        return input.size > std::numeric_limits<std::size_t>::max() ||
                       input.size > std::numeric_limits<std::ptrdiff_t>::max()
                   ? GLYPH_STATUS_RESOURCE_LIMIT
                   : GLYPH_STATUS_INVALID_ARGUMENT;
    }
    if (input.size != 0U &&
        std::find(input.data, input.data + input.size, '\0') !=
            input.data + input.size) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    output.assign(input.data == nullptr ? "" : input.data,
                  static_cast<std::size_t>(input.size));
    if (required && output.empty()) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    return GLYPH_STATUS_OK;
}

glyph_status_t import_byte_span(const glyph_byte_span_t* input,
                                std::span<const std::byte>& output) noexcept {
    if (input == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    if (input->size > std::numeric_limits<std::size_t>::max()) {
        return GLYPH_STATUS_RESOURCE_LIMIT;
    }
    if (input->size != 0U && input->data == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    if (input->size == 0U) {
        output = {};
    } else {
        output = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(input->data),
            static_cast<std::size_t>(input->size));
    }
    return GLYPH_STATUS_OK;
}

glyph_status_t import_sender_config(
    const glyph_sender_config_t& input,
    glyph::session::SenderConfig& output) {
    if (!valid_input_header(input.struct_size, input.abi_version,
                            sizeof(glyph_sender_config_t)) ||
        input.reserved != 0U || input.max_object_size == 0U) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    output.transfer_id = {};
    std::copy(std::begin(input.transfer_id), std::end(input.transfer_id),
              output.transfer_id.begin());
    output.block_size = input.block_size;
    output.shard_size = input.shard_size;
    output.object_limits.max_object_size = input.max_object_size;

    auto status = import_utf8_span(input.display_name, true,
                                   output.display_name);
    if (status != GLYPH_STATUS_OK) {
        return status;
    }
    status = import_utf8_span(input.media_type, false, output.media_type.emplace());
    if (status != GLYPH_STATUS_OK) {
        return status;
    }
    if (input.media_type.size == 0U) {
        output.media_type.reset();
    }
    return import_utf8_span(input.fec_profile, true, output.fec_profile);
}

std::filesystem::path path_from_utf8(const std::string& value) {
    const auto* data = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path(std::u8string(data, value.size()));
}

glyph_status_t import_receiver_config(
    const glyph_receiver_config_t& input,
    glyph::session::ReceiverConfig& output) {
    if (!valid_input_header(input.struct_size, input.abi_version,
                            sizeof(glyph_receiver_config_t)) ||
        input.max_object_size == 0U) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }

    std::string output_root;
    std::string journal_path;
    std::string supported_profile;
    auto status = import_utf8_span(input.output_root, true, output_root);
    if (status != GLYPH_STATUS_OK) {
        return status;
    }
    status = import_utf8_span(input.journal_path, true, journal_path);
    if (status != GLYPH_STATUS_OK) {
        return status;
    }
    status = import_utf8_span(input.supported_fec_profile, true,
                              supported_profile);
    if (status != GLYPH_STATUS_OK) {
        return status;
    }

    output.output_root = path_from_utf8(output_root);
    output.journal_path = path_from_utf8(journal_path);
    output.supported_fec_profile = std::move(supported_profile);
    output.object_limits.max_object_size = input.max_object_size;
    if (input.io_buffer_bytes > std::numeric_limits<std::size_t>::max()) {
        return GLYPH_STATUS_RESOURCE_LIMIT;
    }
    if (input.io_buffer_bytes != 0U) {
        output.object_limits.io_buffer_bytes =
            static_cast<std::size_t>(input.io_buffer_bytes);
    }
    if (input.max_journal_bytes != 0U) {
        output.journal_limits.max_journal_bytes = input.max_journal_bytes;
    }
    if (input.max_shard_receipts != 0U) {
        output.journal_limits.max_shard_receipts = input.max_shard_receipts;
    }
    if (input.max_verified_blocks != 0U) {
        output.journal_limits.max_verified_blocks = input.max_verified_blocks;
    }
    return GLYPH_STATUS_OK;
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()),
                       value.size());
}

}  // namespace

extern "C" {

std::uint32_t glyph_c_abi_version(void) { return kCAbiVersion; }

const char* glyph_status_name_c(const glyph_status_t status) {
    switch (status) {
        case GLYPH_STATUS_OK:
            return "ok";
        case GLYPH_STATUS_INVALID_ARGUMENT:
            return "invalid_argument";
        case GLYPH_STATUS_IO:
            return "io";
        case GLYPH_STATUS_PROTOCOL:
            return "protocol";
        case GLYPH_STATUS_INTEGRITY:
            return "integrity";
        case GLYPH_STATUS_CRYPTO:
            return "crypto";
        case GLYPH_STATUS_CAMERA:
            return "camera";
        case GLYPH_STATUS_DISPLAY:
            return "display";
        case GLYPH_STATUS_RESOURCE_LIMIT:
            return "resource_limit";
        case GLYPH_STATUS_UNSUPPORTED:
            return "unsupported";
        default:
            return "unknown";
    }
}

glyph_status_t glyph_mp0_renderer_create(
    const glyph_mp0_surface_config_t* config,
    glyph_mp0_renderer_t** renderer) {
    if (config == nullptr || renderer == nullptr || *renderer != nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }

    glyph::optical::Mp0SurfaceConfig native_config;
    const auto config_status = import_config(*config, native_config);
    if (config_status != GLYPH_STATUS_OK) {
        return config_status;
    }

    glyph::optical::SurfaceLayout native_layout;
    const auto layout_status = glyph::optical::calculate_mp0_layout(
        native_config, native_layout);
    if (layout_status != glyph::Status::ok) {
        return to_c_status(layout_status);
    }

    try {
        auto candidate = std::make_unique<glyph_mp0_renderer>();
        candidate->config = native_config;
        candidate->layout = native_layout;
        *renderer = candidate.release();
        return GLYPH_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return GLYPH_STATUS_RESOURCE_LIMIT;
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

void glyph_mp0_renderer_destroy(glyph_mp0_renderer_t* renderer) {
    delete renderer;
}

glyph_status_t glyph_mp0_renderer_get_layout(
    const glyph_mp0_renderer_t* renderer,
    glyph_mp0_surface_layout_t* layout) {
    if (renderer == nullptr || layout == nullptr ||
        !valid_layout_output(*layout)) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }

    auto candidate = *layout;
    export_layout(renderer->config, renderer->layout, candidate);
    *layout = candidate;
    return GLYPH_STATUS_OK;
}

glyph_status_t glyph_mp0_renderer_set_frame(
    glyph_mp0_renderer_t* renderer,
    const std::uint32_t frame_seq,
    const std::uint32_t hold_index) {
    if (renderer == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    renderer->config.frame_seq = frame_seq;
    renderer->config.hold_index = hold_index;
    return GLYPH_STATUS_OK;
}

glyph_status_t glyph_mp0_renderer_render(
    glyph_mp0_renderer_t* renderer,
    const std::uint8_t* encoded_frame_header,
    const std::uint64_t encoded_frame_header_bytes,
    const glyph_rgb8_cell_t* payload_cells,
    const std::uint64_t payload_cell_count,
    glyph_mp0_surface_buffer_t* output) {
    if (renderer == nullptr || encoded_frame_header == nullptr ||
        encoded_frame_header_bytes != GLYPH_FRAME_HEADER_BYTES ||
        output == nullptr || !valid_buffer_output(*output)) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }

    const auto pixel_count = static_cast<std::uint64_t>(renderer->config.width) *
                             static_cast<std::uint64_t>(renderer->config.height);
    if (output->pixels == nullptr || output->pixel_capacity < pixel_count ||
        pixel_count > std::numeric_limits<std::size_t>::max() ||
        payload_cell_count > renderer->layout.payload_cell_capacity ||
        payload_cell_count > std::numeric_limits<std::size_t>::max()) {
        return GLYPH_STATUS_RESOURCE_LIMIT;
    }
    if (payload_cell_count != 0U && payload_cells == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }

    try {
        std::array<std::byte, glyph::kFrameHeaderBytes> encoded{};
        for (std::size_t index = 0U; index < encoded.size(); ++index) {
            encoded[index] = static_cast<std::byte>(encoded_frame_header[index]);
        }

        glyph::FrameHeader header;
        const auto header_status = glyph::decode_frame_header(encoded, header);
        if (header_status != glyph::Status::ok) {
            return to_c_status(header_status);
        }

        std::vector<glyph::Rgb8Cell> native_payload;
        native_payload.reserve(static_cast<std::size_t>(payload_cell_count));
        for (std::size_t index = 0U;
             index < static_cast<std::size_t>(payload_cell_count); ++index) {
            native_payload.push_back(glyph::Rgb8Cell{
                payload_cells[index].red,
                payload_cells[index].green,
                payload_cells[index].blue,
            });
        }

        glyph::optical::OpticalSurface surface;
        const auto render_status = glyph::optical::render_mp0_surface(
            renderer->config, header, native_payload, surface);
        if (render_status != glyph::Status::ok) {
            return to_c_status(render_status);
        }

        for (std::size_t index = 0U; index < surface.pixels.size(); ++index) {
            const auto& pixel = surface.pixels[index];
            output->pixels[index] = glyph_rgba8_t{
                pixel.red,
                pixel.green,
                pixel.blue,
                pixel.alpha,
            };
        }

        auto candidate = *output;
        candidate.struct_size = sizeof(glyph_mp0_surface_buffer_t);
        candidate.abi_version = kCAbiVersion;
        candidate.pixel_count = pixel_count;
        candidate.width = surface.width;
        candidate.height = surface.height;
        candidate.frame_seq = surface.frame_seq;
        candidate.hold_index = surface.hold_index;
        *output = candidate;
        return GLYPH_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return GLYPH_STATUS_RESOURCE_LIMIT;
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

std::uint32_t glyph_sender_state(const glyph_sender_t* sender) {
    if (sender == nullptr) {
        return UINT32_MAX;
    }
    return static_cast<std::uint32_t>(sender->session.state());
}

std::uint32_t glyph_receiver_state(const glyph_receiver_t* receiver) {
    if (receiver == nullptr) {
        return UINT32_MAX;
    }
    return static_cast<std::uint32_t>(receiver->session.state());
}

glyph_status_t glyph_sender_create(
    const glyph_byte_span_t* object,
    const glyph_sender_config_t* config,
    glyph_sender_t** sender) {
    if (object == nullptr || config == nullptr || sender == nullptr ||
        *sender != nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }

    try {
        std::span<const std::byte> object_span;
        auto status = import_byte_span(object, object_span);
        if (status != GLYPH_STATUS_OK) {
            return status;
        }
        glyph::session::SenderConfig native_config;
        status = import_sender_config(*config, native_config);
        if (status != GLYPH_STATUS_OK) {
            return status;
        }
        auto candidate = std::make_unique<glyph_sender>();
        const auto create_status = glyph::session::SenderSession::create(
            object_span, native_config, candidate->session);
        if (create_status != glyph::Status::ok) {
            return to_c_status(create_status);
        }
        candidate->block_size = native_config.block_size;
        *sender = candidate.release();
        return GLYPH_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return GLYPH_STATUS_RESOURCE_LIMIT;
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

glyph_status_t glyph_sender_open_file(
    const glyph_utf8_span_t* source_path,
    const glyph_sender_config_t* config,
    glyph_sender_t** sender) {
    if (source_path == nullptr || config == nullptr || sender == nullptr ||
        *sender != nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }

    try {
        std::string native_source_path;
        auto status = import_utf8_span(*source_path, true, native_source_path);
        if (status != GLYPH_STATUS_OK) {
            return status;
        }
        glyph::session::SenderConfig native_config;
        status = import_sender_config(*config, native_config);
        if (status != GLYPH_STATUS_OK) {
            return status;
        }
        auto candidate = std::make_unique<glyph_sender>();
        const auto open_status = glyph::session::SenderSession::open_file(
            path_from_utf8(native_source_path), native_config,
            candidate->session);
        if (open_status != glyph::Status::ok) {
            return to_c_status(open_status);
        }
        candidate->block_size = native_config.block_size;
        *sender = candidate.release();
        return GLYPH_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return GLYPH_STATUS_RESOURCE_LIMIT;
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

void glyph_sender_destroy(glyph_sender_t* sender) { delete sender; }

glyph_status_t glyph_sender_prepare(glyph_sender_t* sender) {
    if (sender == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    try {
        return to_c_status(sender->session.prepare());
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

glyph_status_t glyph_sender_begin_bootstrap(glyph_sender_t* sender) {
    if (sender == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    try {
        return to_c_status(sender->session.begin_bootstrap());
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

glyph_status_t glyph_sender_next_block(
    glyph_sender_t* sender,
    std::uint32_t* block_id,
    std::uint8_t* bytes,
    const std::uint64_t byte_capacity,
    std::uint64_t* bytes_written,
    std::uint32_t* available) {
    if (sender == nullptr || block_id == nullptr || bytes == nullptr ||
        bytes_written == nullptr || available == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    if (sender->block_size > std::numeric_limits<std::size_t>::max() ||
        byte_capacity < sender->block_size) {
        return GLYPH_STATUS_RESOURCE_LIMIT;
    }

    try {
        glyph::session::LogicalBlock block;
        bool native_available = false;
        const auto status = sender->session.next_block(block, native_available);
        if (status != glyph::Status::ok) {
            return to_c_status(status);
        }
        if (native_available) {
            if (block.bytes.size() > byte_capacity) {
                return GLYPH_STATUS_RESOURCE_LIMIT;
            }
            std::memcpy(bytes, block.bytes.data(), block.bytes.size());
        }
        *block_id = block.block_id;
        *bytes_written = static_cast<std::uint64_t>(block.bytes.size());
        *available = native_available ? 1U : 0U;
        return GLYPH_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return GLYPH_STATUS_RESOURCE_LIMIT;
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

glyph_status_t glyph_sender_get_manifest(
    const glyph_sender_t* sender,
    std::uint8_t* bytes,
    const std::uint64_t byte_capacity,
    std::uint64_t* bytes_written) {
    if (sender == nullptr || bytes_written == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    const auto manifest = sender->session.manifest_bytes();
    const auto required = static_cast<std::uint64_t>(manifest.size());
    if (byte_capacity < required || (required != 0U && bytes == nullptr)) {
        *bytes_written = required;
        return byte_capacity < required ? GLYPH_STATUS_RESOURCE_LIMIT
                                        : GLYPH_STATUS_INVALID_ARGUMENT;
    }
    if (required != 0U) {
        std::memcpy(bytes, manifest.data(), manifest.size());
    }
    *bytes_written = required;
    return GLYPH_STATUS_OK;
}

glyph_status_t glyph_sender_complete_final_repeat(glyph_sender_t* sender) {
    if (sender == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    try {
        return to_c_status(sender->session.complete_final_repeat());
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

void glyph_sender_cancel(glyph_sender_t* sender) {
    if (sender != nullptr) {
        sender->session.cancel();
    }
}

glyph_status_t glyph_sender_get_progress(const glyph_sender_t* sender,
                                          std::uint64_t* bytes_emitted) {
    if (sender == nullptr || bytes_emitted == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    *bytes_emitted = sender->session.bytes_emitted();
    return GLYPH_STATUS_OK;
}

glyph_status_t glyph_receiver_create(
    const glyph_receiver_config_t* config,
    glyph_receiver_t** receiver) {
    if (config == nullptr || receiver == nullptr || *receiver != nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }

    try {
        glyph::session::ReceiverConfig native_config;
        const auto import_status = import_receiver_config(*config, native_config);
        if (import_status != GLYPH_STATUS_OK) {
            return import_status;
        }
        auto candidate = std::make_unique<glyph_receiver>();
        const auto create_status = glyph::session::ReceiverSession::create(
            native_config, candidate->session);
        if (create_status != glyph::Status::ok) {
            return to_c_status(create_status);
        }
        *receiver = candidate.release();
        return GLYPH_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return GLYPH_STATUS_RESOURCE_LIMIT;
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

void glyph_receiver_destroy(glyph_receiver_t* receiver) { delete receiver; }

glyph_status_t glyph_receiver_begin_search(glyph_receiver_t* receiver) {
    if (receiver == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    try {
        return to_c_status(receiver->session.begin_search());
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

glyph_status_t glyph_receiver_notify_surface_found(
    glyph_receiver_t* receiver) {
    if (receiver == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    try {
        return to_c_status(receiver->session.notify_surface_found());
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

glyph_status_t glyph_receiver_notify_calibrated(glyph_receiver_t* receiver) {
    if (receiver == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    try {
        return to_c_status(receiver->session.notify_calibrated());
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

glyph_status_t glyph_receiver_submit_manifest(
    glyph_receiver_t* receiver,
    const glyph_byte_span_t* manifest) {
    if (receiver == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    try {
        std::span<const std::byte> native_manifest;
        const auto import_status = import_byte_span(manifest, native_manifest);
        if (import_status != GLYPH_STATUS_OK) {
            return import_status;
        }
        return to_c_status(receiver->session.submit_manifest(native_manifest));
    } catch (const std::bad_alloc&) {
        return GLYPH_STATUS_RESOURCE_LIMIT;
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

glyph_status_t glyph_receiver_submit_block(
    glyph_receiver_t* receiver,
    const std::uint32_t block_id,
    const glyph_byte_span_t* bytes) {
    if (receiver == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    try {
        std::span<const std::byte> native_bytes;
        const auto import_status = import_byte_span(bytes, native_bytes);
        if (import_status != GLYPH_STATUS_OK) {
            return import_status;
        }
        return to_c_status(
            receiver->session.submit_block(block_id, native_bytes));
    } catch (const std::bad_alloc&) {
        return GLYPH_STATUS_RESOURCE_LIMIT;
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

glyph_status_t glyph_receiver_pause(glyph_receiver_t* receiver) {
    if (receiver == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    try {
        return to_c_status(receiver->session.pause());
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

glyph_status_t glyph_receiver_finalize(glyph_receiver_t* receiver) {
    if (receiver == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    try {
        return to_c_status(receiver->session.finalize());
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

void glyph_receiver_cancel(glyph_receiver_t* receiver) {
    if (receiver != nullptr) {
        receiver->session.cancel();
    }
}

glyph_status_t glyph_receiver_get_progress(
    const glyph_receiver_t* receiver,
    std::uint64_t* bytes_received) {
    if (receiver == nullptr || bytes_received == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    *bytes_received = receiver->session.bytes_received();
    return GLYPH_STATUS_OK;
}

glyph_status_t glyph_receiver_get_final_path(
    const glyph_receiver_t* receiver,
    char* output,
    const std::uint64_t capacity,
    std::uint64_t* required_bytes) {
    if (receiver == nullptr || required_bytes == nullptr) {
        return GLYPH_STATUS_INVALID_ARGUMENT;
    }
    try {
        const auto& final_path = receiver->session.final_path();
        if (final_path.empty()) {
            return GLYPH_STATUS_INVALID_ARGUMENT;
        }
        const auto encoded = path_to_utf8(final_path);
        const auto required = static_cast<std::uint64_t>(encoded.size() + 1U);
        if (capacity < required) {
            *required_bytes = required;
            return GLYPH_STATUS_RESOURCE_LIMIT;
        }
        if (output == nullptr) {
            return GLYPH_STATUS_INVALID_ARGUMENT;
        }
        std::memcpy(output, encoded.c_str(), encoded.size() + 1U);
        *required_bytes = required;
        return GLYPH_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return GLYPH_STATUS_RESOURCE_LIMIT;
    } catch (...) {
        return GLYPH_STATUS_UNSUPPORTED;
    }
}

}  // extern "C"
