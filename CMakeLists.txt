cmake_minimum_required(VERSION 3.20)

project(glyph VERSION 0.1.0 LANGUAGES CXX)

option(GLYPH_BUILD_TESTS "Build the GLYPH unit tests" ON)

add_library(glyph_core STATIC
    src/crypto/sha256.cpp
    src/core/status.cpp
    src/frame/crc32c.cpp
    src/frame/frame.cpp
    src/fec/reed_solomon.cpp
    src/manifest/manifest.cpp
    src/phy/rgb8.cpp
    src/transport/object_io.cpp
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

if(GLYPH_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
