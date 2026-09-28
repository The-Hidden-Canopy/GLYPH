#include "glyph/c_api.h"

int main(void) {
    glyph_mp0_surface_config_t config = {0};
    glyph_mp0_surface_layout_t layout = {0};
    glyph_mp0_surface_buffer_t buffer = {0};

    if (GLYPH_C_ABI_VERSION != 1U ||
        sizeof(config) == 0U || sizeof(layout) == 0U ||
        sizeof(buffer) == 0U) {
        return 1;
    }
    return 0;
}
