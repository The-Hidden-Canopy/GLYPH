add_executable(glyph_core_unit_tests
    unit/test_core.cpp
)

target_link_libraries(glyph_core_unit_tests PRIVATE glyph::core)

add_test(NAME glyph.core.unit COMMAND glyph_core_unit_tests)

add_executable(glyph_object_layer_unit_tests
    unit/test_object_layer.cpp
)

target_link_libraries(glyph_object_layer_unit_tests PRIVATE glyph::core)

add_test(NAME glyph.object_layer.unit COMMAND glyph_object_layer_unit_tests)

add_executable(glyph_resume_journal_unit_tests
    unit/test_resume_journal.cpp
)

target_link_libraries(glyph_resume_journal_unit_tests PRIVATE glyph::core)

add_test(NAME glyph.resume_journal.unit COMMAND glyph_resume_journal_unit_tests)

add_executable(glyph_frame_unit_tests
    unit/test_frame.cpp
)

target_link_libraries(glyph_frame_unit_tests PRIVATE glyph::core)

add_test(NAME glyph.frame.unit COMMAND glyph_frame_unit_tests)

add_executable(glyph_scheduler_unit_tests
    unit/test_scheduler.cpp
)

target_link_libraries(glyph_scheduler_unit_tests PRIVATE glyph::core)

add_test(NAME glyph.scheduler.unit COMMAND glyph_scheduler_unit_tests)

add_executable(glyph_fec_unit_tests
    unit/test_fec.cpp
)

target_link_libraries(glyph_fec_unit_tests PRIVATE glyph::core)

add_test(NAME glyph.fec.unit COMMAND glyph_fec_unit_tests)

add_executable(glyph_inner_fec_unit_tests
    unit/test_inner_fec.cpp
)

target_link_libraries(glyph_inner_fec_unit_tests PRIVATE glyph::core)

add_test(NAME glyph.inner_fec.unit COMMAND glyph_inner_fec_unit_tests)

add_executable(glyph_interleaver_unit_tests
    unit/test_interleaver.cpp
)

target_link_libraries(glyph_interleaver_unit_tests PRIVATE glyph::core)

add_test(NAME glyph.interleaver.unit COMMAND glyph_interleaver_unit_tests)

add_executable(glyph_tile_payload_unit_tests
    unit/test_tile_payload.cpp
)

target_link_libraries(glyph_tile_payload_unit_tests PRIVATE glyph::core)

add_test(NAME glyph.tile_payload.unit COMMAND glyph_tile_payload_unit_tests)

add_executable(glyph_rgb8_tile_unit_tests
    unit/test_rgb8_tile.cpp
)

target_link_libraries(glyph_rgb8_tile_unit_tests PRIVATE glyph::core)

add_test(NAME glyph.rgb8_tile.unit COMMAND glyph_rgb8_tile_unit_tests)

add_executable(glyph_rgb8_unit_tests
    unit/test_rgb8.cpp
)

target_link_libraries(glyph_rgb8_unit_tests PRIVATE glyph::core)

add_test(NAME glyph.rgb8.unit COMMAND glyph_rgb8_unit_tests)
