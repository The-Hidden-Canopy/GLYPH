#pragma once

#include "glyph/core/status.hpp"
#include "glyph/crypto/sha256.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace glyph::detail {

// A small, non-buffered file primitive for trust-boundary paths.  It keeps the
// opened descriptor/handle alive so callers do not have to re-open a path
// between validation, writing, hashing, and promotion.
class SecureFile final {
public:
    SecureFile() noexcept;
    ~SecureFile();

    SecureFile(const SecureFile&) = delete;
    SecureFile& operator=(const SecureFile&) = delete;

    SecureFile(SecureFile&& other) noexcept;
    SecureFile& operator=(SecureFile&& other) noexcept;

    [[nodiscard]] static Status create_new(const std::filesystem::path& path,
                                           SecureFile& file) noexcept;
    [[nodiscard]] static Status open_rw(const std::filesystem::path& path,
                                         SecureFile& file) noexcept;
    [[nodiscard]] static Status open_read(const std::filesystem::path& path,
                                           SecureFile& file) noexcept;

    [[nodiscard]] bool is_open() const noexcept;

    [[nodiscard]] Status write(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] Status read_all(std::vector<std::byte>& bytes,
                                  std::uint64_t max_bytes) const noexcept;
    [[nodiscard]] Status hash_sha256(std::uint64_t max_bytes,
                                     std::size_t buffer_bytes,
                                     std::uint64_t& object_size,
                                     Digest256& digest) const noexcept;
    [[nodiscard]] Status size(std::uint64_t& value) const noexcept;
    [[nodiscard]] Status truncate(std::uint64_t value) noexcept;
    [[nodiscard]] Status seek_end() noexcept;
    [[nodiscard]] Status synchronize() noexcept;
    [[nodiscard]] Status lock_exclusive() noexcept;
    [[nodiscard]] Status close() noexcept;

    // Removes the originally opened file only if the directory entry still
    // names that same file.  This prevents abort cleanup from deleting a path
    // that was replaced after the handle was opened.
    [[nodiscard]] Status remove_owned_path() noexcept;

private:
    friend Status promote_no_replace(
        SecureFile& file,
        const std::filesystem::path& temporary_path,
        const std::filesystem::path& final_path) noexcept;

    [[nodiscard]] Status initialize_identity() noexcept;
    [[nodiscard]] bool path_matches_identity() const noexcept;

    std::filesystem::path path_;
    bool locked_ = false;

#if defined(_WIN32)
    void* handle_ = nullptr;
    std::uint32_t volume_serial_ = 0U;
    std::uint32_t file_index_high_ = 0U;
    std::uint32_t file_index_low_ = 0U;
#elif defined(__unix__) || defined(__APPLE__)
    int descriptor_ = -1;
    std::uint64_t device_ = 0U;
    std::uint64_t inode_ = 0U;
#else
    std::intptr_t native_handle_ = -1;
#endif
};

// Promotes an opened temporary file without replacing an existing final path.
// The platform implementations also synchronize the containing directory when
// that operation is available.
[[nodiscard]] Status promote_no_replace(
    SecureFile& file,
    const std::filesystem::path& temporary_path,
    const std::filesystem::path& final_path) noexcept;

}  // namespace glyph::detail
