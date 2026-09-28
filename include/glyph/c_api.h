#ifndef GLYPH_C_API_H
#define GLYPH_C_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GLYPH_C_ABI_VERSION UINT32_C(1)
#define GLYPH_FRAME_HEADER_BYTES UINT32_C(64)

typedef uint32_t glyph_status_t;

enum {
    GLYPH_STATUS_OK = 0,
    GLYPH_STATUS_INVALID_ARGUMENT = 1,
    GLYPH_STATUS_IO = 2,
    GLYPH_STATUS_PROTOCOL = 3,
    GLYPH_STATUS_INTEGRITY = 4,
    GLYPH_STATUS_CRYPTO = 5,
    GLYPH_STATUS_CAMERA = 6,
    GLYPH_STATUS_DISPLAY = 7,
    GLYPH_STATUS_RESOURCE_LIMIT = 8,
    GLYPH_STATUS_UNSUPPORTED = 9,
};

typedef uint32_t glyph_surface_aspect_t;

enum {
    GLYPH_SURFACE_LANDSCAPE_16_9 = 0,
    GLYPH_SURFACE_PORTRAIT_9_16 = 1,
    GLYPH_SURFACE_SQUARE = 2,
};

typedef struct glyph_mp0_renderer glyph_mp0_renderer_t;

typedef struct glyph_rgb8_cell {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} glyph_rgb8_cell_t;

typedef struct glyph_rgba8 {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
} glyph_rgba8_t;

typedef struct glyph_mp0_surface_config {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t width;
    uint32_t height;
    uint16_t cell_pitch;
    uint8_t aspect;
    uint8_t profile_id;
    uint16_t reserved;
    uint32_t frame_seq;
    uint32_t hold_index;
} glyph_mp0_surface_config_t;

typedef struct glyph_mp0_surface_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} glyph_mp0_surface_rect_t;

typedef struct glyph_mp0_surface_layout {
    uint32_t struct_size;
    uint32_t abi_version;
    glyph_mp0_surface_rect_t safe_area;
    glyph_mp0_surface_rect_t control_band;
    glyph_mp0_surface_rect_t payload_area;
    glyph_mp0_surface_rect_t calibration_band;
    uint32_t grid_columns;
    uint32_t grid_rows;
    uint32_t payload_columns;
    uint32_t payload_rows;
    uint64_t payload_cell_capacity;
    uint64_t pixel_count;
} glyph_mp0_surface_layout_t;

// The caller owns pixels and must provide capacity for pixel_count entries.
// On failure, the structure and caller buffer are left unchanged.
typedef struct glyph_mp0_surface_buffer {
    uint32_t struct_size;
    uint32_t abi_version;
    glyph_rgba8_t* pixels;
    uint64_t pixel_capacity;
    uint64_t pixel_count;
    uint32_t width;
    uint32_t height;
    uint32_t frame_seq;
    uint32_t hold_index;
} glyph_mp0_surface_buffer_t;

uint32_t glyph_c_abi_version(void);
const char* glyph_status_name_c(glyph_status_t status);

glyph_status_t glyph_mp0_renderer_create(
    const glyph_mp0_surface_config_t* config,
    glyph_mp0_renderer_t** renderer);

void glyph_mp0_renderer_destroy(glyph_mp0_renderer_t* renderer);

glyph_status_t glyph_mp0_renderer_get_layout(
    const glyph_mp0_renderer_t* renderer,
    glyph_mp0_surface_layout_t* layout);

glyph_status_t glyph_mp0_renderer_set_frame(
    glyph_mp0_renderer_t* renderer,
    uint32_t frame_seq,
    uint32_t hold_index);

glyph_status_t glyph_mp0_renderer_render(
    glyph_mp0_renderer_t* renderer,
    const uint8_t* encoded_frame_header,
    uint64_t encoded_frame_header_bytes,
    const glyph_rgb8_cell_t* payload_cells,
    uint64_t payload_cell_count,
    glyph_mp0_surface_buffer_t* output);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // GLYPH_C_API_H
