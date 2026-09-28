#include "glyph/c_api.h"

#include "glyph/core/status.hpp"
#include "glyph/frame/frame.hpp"
#include "glyph/optical/surface.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <vector>

struct glyph_mp0_renderer {
    glyph::optical::Mp0SurfaceConfig config{};
    glyph::optical::SurfaceLayout layout{};
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

}  // extern "C"
