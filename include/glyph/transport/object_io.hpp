#pragma once

#include "glyph/manifest/manifest.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

namespace glyph {

struct ObjectLimits {
    std::uint64_t max_object_size = 1ULL << 40U;
    std::size_t io_buffer_bytes = 64U << 10U;
};

struct ObjectDigest {
    Status status = Status::ok;
    std::uint64_t object_size = 0;
    Digest256 sha256{};
};

[[nodiscard]] ObjectDigest hash_object_file(
    const std::filesystem::path& path,
    const ObjectLimits& limits = {});

[[nodiscard]] Status verify_object_file(
    const std::filesystem::path& path,
    std::uint64_t expected_size,
    const Digest256& expected_sha256,
    const ObjectLimits& limits = {});

[[nodiscard]] std::string sanitize_display_name(std::string_view display_name);

class AtomicObjectWriter final {
public:
    AtomicObjectWriter() = default;
    ~AtomicObjectWriter();

    AtomicObjectWriter(const AtomicObjectWriter&) = delete;
    AtomicObjectWriter& operator=(const AtomicObjectWriter&) = delete;

    AtomicObjectWriter(AtomicObjectWriter&& other) noexcept;
    AtomicObjectWriter& operator=(AtomicObjectWriter&& other) noexcept;

    [[nodiscard]] static Status open(
        const std::filesystem::path& output_root,
        std::string_view display_name,
        const ObjectLimits& limits,
        AtomicObjectWriter& writer);

    [[nodiscard]] Status write(std::span<const std::byte> bytes);

    [[nodiscard]] Status finalize(std::uint64_t expected_size,
                                  const Digest256& expected_sha256);

    void abort() noexcept;

    [[nodiscard]] const std::filesystem::path& final_path() const noexcept {
        return final_path_;
    }

    [[nodiscard]] const std::filesystem::path& temporary_path() const noexcept {
        return temporary_path_;
    }

private:
    std::ofstream stream_;
    std::filesystem::path final_path_;
    std::filesystem::path temporary_path_;
    ObjectLimits limits_{};
    std::uint64_t object_size_ = 0;
    bool open_ = false;
};

}  // namespace glyph

