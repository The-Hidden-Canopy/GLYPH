#pragma once

#include "glyph/manifest/manifest.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace glyph {

namespace detail {
class SecureFile;
}

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
    AtomicObjectWriter() noexcept;
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

    // Opens the identity-bound partial object used by restartable sessions.
    // A missing partial is created; an existing partial is truncated to
    // resume_size before more bytes are accepted. The final path must not
    // already exist.
    [[nodiscard]] static Status open_resumable(
        const std::filesystem::path& output_root,
        std::string_view display_name,
        const Digest256& resume_key,
        std::uint64_t resume_size,
        const ObjectLimits& limits,
        AtomicObjectWriter& writer);

    [[nodiscard]] Status write(std::span<const std::byte> bytes);

    // Confirms that the current secure file handle is still open. Writes are
    // unbuffered at this layer; durable_checkpoint() is the OS sync boundary.
    [[nodiscard]] Status checkpoint();

    // Synchronizes the open file handle through the host OS. The handle is not
    // closed and re-opened by path, preserving the trust boundary.
    [[nodiscard]] Status durable_checkpoint();

    // Flushes and closes the partial stream while retaining the deterministic
    // partial file for a later open_resumable call. Explicit abort() still
    // discards it.
    [[nodiscard]] Status suspend();

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
    std::unique_ptr<detail::SecureFile> file_;
    std::filesystem::path final_path_;
    std::filesystem::path temporary_path_;
    ObjectLimits limits_{};
    std::uint64_t object_size_ = 0;
    bool open_ = false;
    bool preserve_on_destroy_ = false;
};

}  // namespace glyph
