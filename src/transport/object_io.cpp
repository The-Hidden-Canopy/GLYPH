#include "glyph/transport/object_io.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <limits>
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

bool checked_add_exceeds(const std::uint64_t current,
                         const std::size_t incoming,
                         const std::uint64_t maximum) {
    if (incoming > std::numeric_limits<std::uint64_t>::max() - current) {
        return true;
    }
    return current + static_cast<std::uint64_t>(incoming) > maximum;
}

}  // namespace

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
            if (hasher.update(std::span<const std::byte>(buffer.data(), bytes_read)) !=
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
}

Status verify_object_file(const std::filesystem::path& path,
                          const std::uint64_t expected_size,
                          const Digest256& expected_sha256,
                          const ObjectLimits& limits) {
    const auto actual = hash_object_file(path, limits);
    if (actual.status != Status::ok) {
        return actual.status;
    }
    if (actual.object_size != expected_size || actual.sha256 != expected_sha256) {
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
    abort();
}

AtomicObjectWriter::AtomicObjectWriter(AtomicObjectWriter&& other) noexcept
    : stream_(std::move(other.stream_)),
      final_path_(std::move(other.final_path_)),
      temporary_path_(std::move(other.temporary_path_)),
      limits_(other.limits_),
      object_size_(other.object_size_),
      open_(other.open_) {
    other.object_size_ = 0U;
    other.open_ = false;
    other.temporary_path_.clear();
}

AtomicObjectWriter& AtomicObjectWriter::operator=(AtomicObjectWriter&& other) noexcept {
    if (this != &other) {
        abort();
        stream_ = std::move(other.stream_);
        final_path_ = std::move(other.final_path_);
        temporary_path_ = std::move(other.temporary_path_);
        limits_ = other.limits_;
        object_size_ = other.object_size_;
        open_ = other.open_;
        other.object_size_ = 0U;
        other.open_ = false;
        other.temporary_path_.clear();
    }
    return *this;
}

Status AtomicObjectWriter::open(const std::filesystem::path& output_root,
                                const std::string_view display_name,
                                const ObjectLimits& limits,
                                AtomicObjectWriter& writer) {
    writer.abort();
    if (limits.max_object_size == 0U || limits.io_buffer_bytes == 0U ||
        limits.io_buffer_bytes > (16U << 20U)) {
        return Status::invalid_argument;
    }

    std::error_code error;
    if (!std::filesystem::is_directory(output_root, error) || error) {
        return Status::io;
    }

    const auto safe_name = sanitize_display_name(display_name);
    writer.final_path_ = output_root / safe_name;
    if (std::filesystem::exists(writer.final_path_, error) || error) {
        writer.final_path_.clear();
        return Status::io;
    }

    for (unsigned int attempt = 0U; attempt < 8U; ++attempt) {
        writer.temporary_path_ = make_temporary_path(output_root, safe_name);
        if (std::filesystem::exists(writer.temporary_path_, error)) {
            continue;
        }
        writer.stream_.open(writer.temporary_path_,
                            std::ios::binary | std::ios::out | std::ios::trunc);
        if (writer.stream_.is_open()) {
            writer.limits_ = limits;
            writer.object_size_ = 0U;
            writer.open_ = true;
            return Status::ok;
        }
    }

    writer.final_path_.clear();
    writer.temporary_path_.clear();
    return Status::io;
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
        stream_.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        if (!stream_.good()) {
            return Status::io;
        }
    }
    object_size_ += static_cast<std::uint64_t>(bytes.size());
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

    stream_.flush();
    if (!stream_.good()) {
        abort();
        return Status::io;
    }
    stream_.close();

    const auto actual = hash_object_file(temporary_path_, limits_);
    if (actual.status != Status::ok) {
        abort();
        return actual.status;
    }
    if (actual.object_size != expected_size || actual.sha256 != expected_sha256) {
        abort();
        return Status::integrity;
    }

    std::error_code error;
    if (std::filesystem::exists(final_path_, error) || error) {
        abort();
        return Status::io;
    }
    std::filesystem::rename(temporary_path_, final_path_, error);
    if (error) {
        abort();
        return Status::io;
    }

    temporary_path_.clear();
    open_ = false;
    return Status::ok;
}

void AtomicObjectWriter::abort() noexcept {
    if (stream_.is_open()) {
        stream_.close();
    }
    if (!temporary_path_.empty()) {
        std::error_code error;
        std::filesystem::remove(temporary_path_, error);
    }
    temporary_path_.clear();
    final_path_.clear();
    object_size_ = 0U;
    open_ = false;
}

}  // namespace glyph

