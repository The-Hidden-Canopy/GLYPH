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

add_executable(glyph_frame_unit_tests
    unit/test_frame.cpp
)

target_link_libraries(glyph_frame_unit_tests PRIVATE glyph::core)

add_test(NAME glyph.frame.unit COMMAND glyph_frame_unit_tests)

add_executable(glyph_fec_unit_tests
    unit/test_fec.cpp
)

target_link_libraries(glyph_fec_unit_tests PRIVATE glyph::core)

add_test(NAME glyph.fec.unit COMMAND glyph_fec_unit_tests)

add_executable(glyph_rgb8_unit_tests
    unit/test_rgb8.cpp
)

target_link_libraries(glyph_rgb8_unit_tests PRIVATE glyph::core)

add_test(NAME glyph.rgb8.unit COMMAND glyph_rgb8_unit_tests)
