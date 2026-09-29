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

typedef struct glyph_byte_span {
    const uint8_t* data;
    uint64_t size;
} glyph_byte_span_t;

typedef struct glyph_utf8_span {
    const char* data;
    uint64_t size;
} glyph_utf8_span_t;

typedef uint32_t glyph_sender_state_t;

enum {
    GLYPH_STATE_UNKNOWN = UINT32_MAX,
    GLYPH_SENDER_IDLE = 0,
    GLYPH_SENDER_HASHING = 1,
    GLYPH_SENDER_MANIFEST_READY = 2,
    GLYPH_SENDER_BOOTSTRAP = 3,
    GLYPH_SENDER_TRANSMITTING = 4,
    GLYPH_SENDER_FINAL_REPEAT = 5,
    GLYPH_SENDER_DONE = 6,
    GLYPH_SENDER_CANCELLED = 7,
    GLYPH_SENDER_INVALID_MANIFEST = 8,
    GLYPH_SENDER_RESOURCE_LIMIT = 9,
    GLYPH_SENDER_INTEGRITY_FAILURE = 10,
    GLYPH_SENDER_STORAGE_FAILURE = 11,
};

typedef uint32_t glyph_receiver_state_t;

enum {
    GLYPH_RECEIVER_IDLE = 0,
    GLYPH_RECEIVER_SEARCHING = 1,
    GLYPH_RECEIVER_SURFACE_FOUND = 2,
    GLYPH_RECEIVER_CALIBRATING = 3,
    GLYPH_RECEIVER_MANIFEST = 4,
    GLYPH_RECEIVER_RECEIVING = 5,
    GLYPH_RECEIVER_PAUSED = 6,
    GLYPH_RECEIVER_RECOVERING = 7,
    GLYPH_RECEIVER_VERIFYING = 8,
    GLYPH_RECEIVER_COMPLETE = 9,
    GLYPH_RECEIVER_CANCELLED = 10,
    GLYPH_RECEIVER_UNSUPPORTED_PROFILE = 11,
    GLYPH_RECEIVER_INVALID_MANIFEST = 12,
    GLYPH_RECEIVER_RESOURCE_LIMIT = 13,
    GLYPH_RECEIVER_INTEGRITY_FAILURE = 14,
    GLYPH_RECEIVER_STORAGE_FAILURE = 15,
    GLYPH_RECEIVER_CAMERA_FAILURE = 16,
};

typedef struct glyph_sender_config {
    uint32_t struct_size;
    uint32_t abi_version;
    uint8_t transfer_id[16];
    uint32_t reserved;
    uint64_t block_size;
    uint64_t shard_size;
    glyph_utf8_span_t display_name;
    glyph_utf8_span_t media_type;
    glyph_utf8_span_t fec_profile;
    uint64_t max_object_size;
} glyph_sender_config_t;

typedef struct glyph_receiver_config {
    uint32_t struct_size;
    uint32_t abi_version;
    glyph_utf8_span_t output_root;
    glyph_utf8_span_t journal_path;
    glyph_utf8_span_t supported_fec_profile;
    uint64_t max_object_size;
    uint64_t io_buffer_bytes;
    uint64_t max_journal_bytes;
    uint32_t max_shard_receipts;
    uint32_t max_verified_blocks;
} glyph_receiver_config_t;

typedef struct glyph_sender glyph_sender_t;
typedef struct glyph_receiver glyph_receiver_t;

uint32_t glyph_sender_state(const glyph_sender_t* sender);
uint32_t glyph_receiver_state(const glyph_receiver_t* receiver);

glyph_status_t glyph_sender_create(
    const glyph_byte_span_t* object,
    const glyph_sender_config_t* config,
    glyph_sender_t** sender);

// Opens a bounded local source file. The source is hashed before the manifest
// is emitted and re-verified before the sender enters FINAL_REPEAT.
glyph_status_t glyph_sender_open_file(
    const glyph_utf8_span_t* source_path,
    const glyph_sender_config_t* config,
    glyph_sender_t** sender);

void glyph_sender_destroy(glyph_sender_t* sender);

glyph_status_t glyph_sender_prepare(glyph_sender_t* sender);
glyph_status_t glyph_sender_begin_bootstrap(glyph_sender_t* sender);

// The caller must provide capacity for at least the configured block_size.
// No output buffer or result field is modified on failure.
glyph_status_t glyph_sender_next_block(
    glyph_sender_t* sender,
    uint32_t* block_id,
    uint8_t* bytes,
    uint64_t byte_capacity,
    uint64_t* bytes_written,
    uint32_t* available);

glyph_status_t glyph_sender_get_manifest(
    const glyph_sender_t* sender,
    uint8_t* bytes,
    uint64_t byte_capacity,
    uint64_t* bytes_written);

glyph_status_t glyph_sender_complete_final_repeat(glyph_sender_t* sender);
void glyph_sender_cancel(glyph_sender_t* sender);

glyph_status_t glyph_sender_get_progress(
    const glyph_sender_t* sender,
    uint64_t* bytes_emitted);

glyph_status_t glyph_receiver_create(
    const glyph_receiver_config_t* config,
    glyph_receiver_t** receiver);

void glyph_receiver_destroy(glyph_receiver_t* receiver);

glyph_status_t glyph_receiver_begin_search(glyph_receiver_t* receiver);
glyph_status_t glyph_receiver_notify_surface_found(
    glyph_receiver_t* receiver);
glyph_status_t glyph_receiver_notify_calibrated(glyph_receiver_t* receiver);
glyph_status_t glyph_receiver_submit_manifest(
    glyph_receiver_t* receiver,
    const glyph_byte_span_t* manifest);
glyph_status_t glyph_receiver_submit_block(
    glyph_receiver_t* receiver,
    uint32_t block_id,
    const glyph_byte_span_t* bytes);
glyph_status_t glyph_receiver_pause(glyph_receiver_t* receiver);
glyph_status_t glyph_receiver_finalize(glyph_receiver_t* receiver);
void glyph_receiver_cancel(glyph_receiver_t* receiver);

glyph_status_t glyph_receiver_get_progress(
    const glyph_receiver_t* receiver,
    uint64_t* bytes_received);

// Returns a UTF-8 path including its terminating NUL. If capacity is too
// small, required_bytes is set and the caller buffer is unchanged.
glyph_status_t glyph_receiver_get_final_path(
    const glyph_receiver_t* receiver,
    char* output,
    uint64_t capacity,
    uint64_t* required_bytes);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // GLYPH_C_API_H
