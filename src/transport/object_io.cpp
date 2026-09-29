#include "glyph/transport/object_io.hpp"

#include "secure_file.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <limits>
#include <new>
#include <system_error>
#include <utility>
#include <vector>

namespace glyph {
namespace {

std::atomic<std::uint64_t> temporary_counter{0};

bool is_reserved_windows_name(const std::string& value) {
    std::string stem;
    const auto dot = value.find('.');
    stem = value.substr(0U, dot);
    std::transform(stem.begin(), stem.end(), stem.begin(), [](const char ch) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    });

    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") {
        return true;
    }
    if (stem.size() == 4U &&
        (stem.rfind("COM", 0U) == 0U || stem.rfind("LPT", 0U) == 0U) &&
        stem[3U] >= '1' && stem[3U] <= '9') {
        return true;
    }
    return false;
}

std::filesystem::path make_temporary_path(
    const std::filesystem::path& root,
    const std::string& safe_name) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto sequence = temporary_counter.fetch_add(1U);
    const auto suffix = ".glyph-partial-" + std::to_string(now) + "-" +
                        std::to_string(sequence);
    return root / (safe_name + suffix);
}

std::filesystem::path make_resumable_path(
    const std::filesystem::path& root,
    const std::string& safe_name,
    const Digest256& resume_key) {
    constexpr std::size_t kResumableNamePrefixBytes = 128U;
    const auto prefix = safe_name.substr(
        0U, std::min(safe_name.size(), kResumableNamePrefixBytes));
    return root / (prefix + ".glyph-partial-" + digest_hex(resume_key));
}

bool checked_add_exceeds(const std::uint64_t current,
                         const std::size_t incoming,
                         const std::uint64_t maximum) {
    if (incoming > std::numeric_limits<std::uint64_t>::max() - current) {
        return true;
    }
    return current + static_cast<std::uint64_t>(incoming) > maximum;
}

bool is_symlink_entry(const std::filesystem::path& path,
                      std::error_code& error) noexcept {
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory) {
        error.clear();
        return false;
    }
    return !error && status.type() == std::filesystem::file_type::symlink;
}

}  // namespace

AtomicObjectWriter::AtomicObjectWriter() noexcept = default;

ObjectDigest hash_object_file(const std::filesystem::path& path,
                              const ObjectLimits& limits) {
    ObjectDigest result{};
    if (limits.max_object_size == 0U || limits.io_buffer_bytes == 0U ||
        limits.io_buffer_bytes > (16U << 20U)) {
        result.status = Status::invalid_argument;
        return result;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        result.status = Status::io;
        return result;
    }

    try {
        std::vector<std::byte> buffer(limits.io_buffer_bytes);
        Sha256 hasher;

        while (true) {
            stream.read(reinterpret_cast<char*>(buffer.data()),
                        static_cast<std::streamsize>(buffer.size()));
            const auto count = stream.gcount();
            if (count > 0) {
                const auto bytes_read = static_cast<std::size_t>(count);
                if (checked_add_exceeds(result.object_size, bytes_read,
                                        limits.max_object_size)) {
                    result.status = Status::resource_limit;
                    return result;
                }
                result.object_size += static_cast<std::uint64_t>(bytes_read);
                if (hasher.update(
                        std::span<const std::byte>(buffer.data(), bytes_read)) !=
                    Status::ok) {
                    result.status = Status::resource_limit;
                    return result;
                }
            }

            if (stream.eof()) {
                break;
            }
            if (!stream.good()) {
                result.status = Status::io;
                return result;
            }
        }

        result.status = hasher.finalize(result.sha256);
        return result;
    } catch (const std::bad_alloc&) {
        result.status = Status::resource_limit;
        return result;
    }
}

Status verify_object_file(const std::filesystem::path& path,
                          const std::uint64_t expected_size,
                          const Digest256& expected_sha256,
                          const ObjectLimits& limits) {
    if (limits.max_object_size == 0U || limits.io_buffer_bytes == 0U ||
        limits.io_buffer_bytes > (16U << 20U)) {
        return Status::invalid_argument;
    }

    detail::SecureFile file;
    const auto open_status = detail::SecureFile::open_read(path, file);
    if (open_status != Status::ok) {
        return open_status;
    }
    std::uint64_t actual_size = 0U;
    Digest256 actual_digest{};
    const auto hash_status = file.hash_sha256(
        limits.max_object_size, limits.io_buffer_bytes, actual_size,
        actual_digest);
    static_cast<void>(file.close());
    if (hash_status != Status::ok) {
        return hash_status;
    }
    if (actual_size != expected_size || actual_digest != expected_sha256) {
        return Status::integrity;
    }
    return Status::ok;
}

std::string sanitize_display_name(const std::string_view display_name) {
    std::string safe;
    safe.reserve(std::min<std::size_t>(display_name.size(), 255U));
    for (const auto character : display_name) {
        const auto byte = static_cast<unsigned char>(character);
        const bool invalid = byte < 0x20U || byte == 0x7fU || character == '/' ||
                             character == '\\' || character == ':' ||
                             character == '*' || character == '?' ||
                             character == '"' || character == '<' ||
                             character == '>' || character == '|';
        safe.push_back(invalid ? '_' : character);
        if (safe.size() == 255U) {
            break;
        }
    }

    while (!safe.empty() && (safe.back() == '.' || safe.back() == ' ')) {
        safe.pop_back();
    }
    if (safe.empty() || safe == "." || safe == "..") {
        safe = "glyph-object.bin";
    }
    if (is_reserved_windows_name(safe)) {
        safe.insert(safe.begin(), '_');
    }
    return safe;
}

AtomicObjectWriter::~AtomicObjectWriter() {
    if (preserve_on_destroy_) {
        if (file_ != nullptr) {
            static_cast<void>(file_->close());
            file_.reset();
        }
        temporary_path_.clear();
        final_path_.clear();
        object_size_ = 0U;
        open_ = false;
        preserve_on_destroy_ = false;
        return;
    }
    abort();
}

AtomicObjectWriter::AtomicObjectWriter(AtomicObjectWriter&& other) noexcept
    : file_(std::move(other.file_)),
      final_path_(std::move(other.final_path_)),
      temporary_path_(std::move(other.temporary_path_)),
      limits_(other.limits_),
      object_size_(other.object_size_),
      open_(other.open_),
      preserve_on_destroy_(other.preserve_on_destroy_) {
    other.object_size_ = 0U;
    other.open_ = false;
    other.preserve_on_destroy_ = false;
    other.temporary_path_.clear();
}

AtomicObjectWriter& AtomicObjectWriter::operator=(AtomicObjectWriter&& other) noexcept {
    if (this != &other) {
        abort();
        file_ = std::move(other.file_);
        final_path_ = std::move(other.final_path_);
        temporary_path_ = std::move(other.temporary_path_);
        limits_ = other.limits_;
        object_size_ = other.object_size_;
        open_ = other.open_;
        preserve_on_destroy_ = other.preserve_on_destroy_;
        other.object_size_ = 0U;
        other.open_ = false;
        other.preserve_on_destroy_ = false;
        other.temporary_path_.clear();
    }
    return *this;
}

Status AtomicObjectWriter::open(const std::filesystem::path& output_root,
                                const std::string_view display_name,
                                const ObjectLimits& limits,
                                AtomicObjectWriter& writer) {
    writer.abort();
    writer.preserve_on_destroy_ = false;
    if (limits.max_object_size == 0U || limits.io_buffer_bytes == 0U ||
        limits.io_buffer_bytes > (16U << 20U)) {
        return Status::invalid_argument;
    }

    try {
        std::error_code error;
        if (!std::filesystem::is_directory(output_root, error) || error) {
            return Status::io;
        }

        const auto safe_name = sanitize_display_name(display_name);
        writer.final_path_ = output_root / safe_name;
        if (is_symlink_entry(writer.final_path_, error) || error) {
            writer.final_path_.clear();
            return Status::io;
        }
        if (std::filesystem::exists(writer.final_path_, error) || error) {
            writer.final_path_.clear();
            return Status::io;
        }

        for (unsigned int attempt = 0U; attempt < 8U; ++attempt) {
            writer.temporary_path_ = make_temporary_path(output_root, safe_name);
            writer.file_ = std::make_unique<detail::SecureFile>();
            const auto open_status = detail::SecureFile::create_new(
                writer.temporary_path_, *writer.file_);
            const auto lock_status =
                open_status == Status::ok ? writer.file_->lock_exclusive()
                                          : open_status;
            if (lock_status == Status::ok) {
                writer.limits_ = limits;
                writer.object_size_ = 0U;
                writer.open_ = true;
                writer.preserve_on_destroy_ = false;
                return Status::ok;
            }
            if (open_status == Status::ok) {
                static_cast<void>(writer.file_->close());
                static_cast<void>(writer.file_->remove_owned_path());
            }
            writer.file_.reset();
        }

        writer.final_path_.clear();
        writer.temporary_path_.clear();
        return Status::io;
    } catch (const std::bad_alloc&) {
        writer.abort();
        return Status::resource_limit;
    }
}

Status AtomicObjectWriter::open_resumable(
    const std::filesystem::path& output_root,
    const std::string_view display_name,
    const Digest256& resume_key,
    const std::uint64_t resume_size,
    const ObjectLimits& limits,
    AtomicObjectWriter& writer) {
    writer.abort();
    writer.preserve_on_destroy_ = false;
    if (limits.max_object_size == 0U || limits.io_buffer_bytes == 0U ||
        limits.io_buffer_bytes > (16U << 20U) ||
        resume_size > limits.max_object_size) {
        return Status::invalid_argument;
    }

    try {
        std::error_code error;
        if (!std::filesystem::is_directory(output_root, error) || error) {
            return Status::io;
        }

        const auto safe_name = sanitize_display_name(display_name);
        writer.final_path_ = output_root / safe_name;
        const auto final_is_symlink =
            is_symlink_entry(writer.final_path_, error);
        if (error) {
            writer.final_path_.clear();
            return Status::io;
        }
        if (final_is_symlink) {
            writer.final_path_.clear();
            return Status::integrity;
        }
        if (std::filesystem::exists(writer.final_path_, error) || error) {
            writer.final_path_.clear();
            return Status::io;
        }

        writer.temporary_path_ =
            make_resumable_path(output_root, safe_name, resume_key);
        const auto partial_is_symlink =
            is_symlink_entry(writer.temporary_path_, error);
        if (error) {
            writer.final_path_.clear();
            writer.temporary_path_.clear();
            return Status::io;
        }
        if (partial_is_symlink) {
            writer.final_path_.clear();
            writer.temporary_path_.clear();
            return Status::integrity;
        }
        const bool partial_exists =
            std::filesystem::exists(writer.temporary_path_, error);
        if (error) {
            writer.final_path_.clear();
            writer.temporary_path_.clear();
            return Status::io;
        }

        writer.file_ = std::make_unique<detail::SecureFile>();
        Status file_status = Status::io;
        if (partial_exists) {
            file_status = detail::SecureFile::open_rw(
                writer.temporary_path_, *writer.file_);
            if (file_status != Status::ok) {
                writer.file_.reset();
                writer.final_path_.clear();
                writer.temporary_path_.clear();
                return file_status;
            }
            file_status = writer.file_->lock_exclusive();
            if (file_status != Status::ok) {
                static_cast<void>(writer.file_->close());
                writer.file_.reset();
                writer.final_path_.clear();
                writer.temporary_path_.clear();
                return file_status;
            }
            std::uint64_t partial_size = 0U;
            file_status = writer.file_->size(partial_size);
            if (file_status != Status::ok) {
                writer.file_.reset();
                writer.final_path_.clear();
                writer.temporary_path_.clear();
                return file_status;
            }
            if (partial_size > limits.max_object_size ||
                resume_size > partial_size) {
                static_cast<void>(writer.file_->close());
                writer.file_.reset();
                writer.final_path_.clear();
                writer.temporary_path_.clear();
                return resume_size > partial_size ? Status::integrity
                                                  : Status::resource_limit;
            }
            if (partial_size != resume_size) {
                file_status = writer.file_->truncate(resume_size);
                if (file_status != Status::ok) {
                    writer.file_.reset();
                    writer.final_path_.clear();
                    writer.temporary_path_.clear();
                    return file_status;
                }
            }
        } else {
            if (resume_size != 0U) {
                writer.file_.reset();
                writer.final_path_.clear();
                writer.temporary_path_.clear();
                return Status::integrity;
            }
            file_status = detail::SecureFile::create_new(
                writer.temporary_path_, *writer.file_);
            if (file_status != Status::ok) {
                writer.file_.reset();
                writer.final_path_.clear();
                writer.temporary_path_.clear();
                return file_status;
            }
            file_status = writer.file_->lock_exclusive();
            if (file_status != Status::ok) {
                static_cast<void>(writer.file_->close());
                static_cast<void>(writer.file_->remove_owned_path());
                writer.file_.reset();
                writer.final_path_.clear();
                writer.temporary_path_.clear();
                return file_status;
            }
        }

        if (!writer.file_->is_open()) {
            writer.file_.reset();
            writer.final_path_.clear();
            writer.temporary_path_.clear();
            return Status::io;
        }
        if (writer.file_->seek_end() != Status::ok) {
            static_cast<void>(writer.file_->close());
            writer.file_.reset();
            writer.final_path_.clear();
            writer.temporary_path_.clear();
            return Status::io;
        }
        writer.limits_ = limits;
        writer.object_size_ = resume_size;
        writer.open_ = true;
        writer.preserve_on_destroy_ = false;
        return Status::ok;
    } catch (const std::bad_alloc&) {
        writer.file_.reset();
        writer.final_path_.clear();
        writer.temporary_path_.clear();
        writer.object_size_ = 0U;
        writer.open_ = false;
        writer.preserve_on_destroy_ = false;
        return Status::resource_limit;
    }
}

Status AtomicObjectWriter::write(const std::span<const std::byte> bytes) {
    if (!open_) {
        return Status::invalid_argument;
    }
    if (checked_add_exceeds(object_size_, bytes.size(),
                            limits_.max_object_size)) {
        return Status::resource_limit;
    }
    if (!bytes.empty()) {
        if (file_ == nullptr || file_->write(bytes) != Status::ok) {
            return file_ == nullptr ? Status::invalid_argument : Status::io;
        }
    }
    object_size_ += static_cast<std::uint64_t>(bytes.size());
    return Status::ok;
}

Status AtomicObjectWriter::checkpoint() {
    if (!open_ || file_ == nullptr || !file_->is_open()) {
        return Status::invalid_argument;
    }
    return Status::ok;
}

Status AtomicObjectWriter::durable_checkpoint() {
    if (!open_ || file_ == nullptr || !file_->is_open()) {
        return Status::invalid_argument;
    }
    return file_->synchronize();
}

Status AtomicObjectWriter::suspend() {
    if (!open_) {
        return Status::invalid_argument;
    }
    const auto checkpoint_status = durable_checkpoint();
    if (checkpoint_status != Status::ok) {
        abort();
        return checkpoint_status;
    }
    static_cast<void>(file_->close());
    file_.reset();
    open_ = false;
    final_path_.clear();
    preserve_on_destroy_ = true;
    return Status::ok;
}

Status AtomicObjectWriter::finalize(const std::uint64_t expected_size,
                                    const Digest256& expected_sha256) {
    if (!open_) {
        return Status::invalid_argument;
    }
    if (object_size_ != expected_size) {
        abort();
        return Status::integrity;
    }

    const auto checkpoint_status = durable_checkpoint();
    if (checkpoint_status != Status::ok) {
        abort();
        return checkpoint_status;
    }
    if (file_ == nullptr) {
        abort();
        return Status::io;
    }
    std::uint64_t actual_size = 0U;
    Digest256 actual_digest{};
    const auto hash_status = file_->hash_sha256(
        limits_.max_object_size, limits_.io_buffer_bytes, actual_size,
        actual_digest);
    if (hash_status != Status::ok) {
        abort();
        return hash_status;
    }
    if (actual_size != expected_size || actual_digest != expected_sha256) {
        abort();
        return Status::integrity;
    }

    std::error_code error;
    if (is_symlink_entry(final_path_, error) || error) {
        abort();
        return Status::io;
    }
    if (std::filesystem::exists(final_path_, error) || error) {
        abort();
        return Status::io;
    }
    const auto promote_status = detail::promote_no_replace(
        *file_, temporary_path_, final_path_);
    if (promote_status != Status::ok) {
        abort();
        return promote_status;
    }

    static_cast<void>(file_->close());
    file_.reset();
    temporary_path_.clear();
    open_ = false;
    preserve_on_destroy_ = false;
    return Status::ok;
}

void AtomicObjectWriter::abort() noexcept {
    if (file_ != nullptr) {
        static_cast<void>(file_->close());
        if (!temporary_path_.empty()) {
            static_cast<void>(file_->remove_owned_path());
        }
        file_.reset();
    }
    temporary_path_.clear();
    final_path_.clear();
    object_size_ = 0U;
    open_ = false;
    preserve_on_destroy_ = false;
}

}  // namespace glyph
