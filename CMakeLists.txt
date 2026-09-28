cmake_minimum_required(VERSION 3.20)

project(glyph VERSION 0.1.0 LANGUAGES CXX C)

option(GLYPH_BUILD_TESTS "Build the GLYPH unit tests" ON)
option(GLYPH_ENABLE_SANITIZERS
       "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
       OFF)

add_library(glyph_core STATIC
    src/c_api.cpp
    src/crypto/sha256.cpp
    src/core/status.cpp
    src/frame/crc32c.cpp
    src/frame/frame.cpp
    src/frame/scheduler.cpp
    src/fec/reed_solomon.cpp
    src/fec/inner_reed_solomon.cpp
    src/frame/interleaver.cpp
    src/frame/tile_payload.cpp
    src/image/png.cpp
    src/manifest/manifest.cpp
    src/optical/surface.cpp
    src/phy/rgb8.cpp
    src/phy/rgb8_tile.cpp
    src/sim/simulator.cpp
    src/transport/object_io.cpp
    src/transport/resume_journal.cpp
)

add_library(glyph::core ALIAS glyph_core)

target_compile_features(glyph_core PUBLIC cxx_std_20)

target_include_directories(glyph_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(glyph_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(glyph_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(GLYPH_ENABLE_SANITIZERS)
    if(MSVC)
        target_compile_options(glyph_core PUBLIC /fsanitize=address)
        target_link_options(glyph_core PUBLIC /fsanitize=address)
    else()
        target_compile_options(glyph_core PUBLIC
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(glyph_core PUBLIC
            -fsanitize=address,undefined
        )
    endif()
endif()

if(GLYPH_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
