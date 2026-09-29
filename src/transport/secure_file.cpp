#include "secure_file.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <limits>
#include <new>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace glyph::detail {
namespace {

#if defined(_WIN32)
constexpr void* kInvalidHandle = nullptr;
#endif

bool checked_add_exceeds(const std::uint64_t current,
                         const std::size_t incoming,
                         const std::uint64_t maximum) noexcept {
    return incoming > std::numeric_limits<std::uint64_t>::max() - current ||
           current + static_cast<std::uint64_t>(incoming) > maximum;
}

#if defined(__unix__) || defined(__APPLE__)
Status synchronize_directory(const std::filesystem::path& path) noexcept {
    auto directory = path.parent_path();
    if (directory.empty()) {
        directory = ".";
    }
    const auto descriptor = ::open(directory.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return Status::io;
    }
    const auto synced = ::fsync(descriptor) == 0;
    const auto closed = ::close(descriptor) == 0;
    return synced && closed ? Status::ok : Status::io;
}
#endif

}  // namespace

SecureFile::SecureFile() noexcept = default;

SecureFile::~SecureFile() {
    static_cast<void>(close());
}

SecureFile::SecureFile(SecureFile&& other) noexcept
    : path_(std::move(other.path_)), locked_(other.locked_)
#if defined(_WIN32)
      , handle_(other.handle_), volume_serial_(other.volume_serial_),
      file_index_high_(other.file_index_high_),
      file_index_low_(other.file_index_low_)
#elif defined(__unix__) || defined(__APPLE__)
      , descriptor_(other.descriptor_), device_(other.device_),
      inode_(other.inode_)
#else
      , native_handle_(other.native_handle_)
#endif
{
    other.locked_ = false;
#if defined(_WIN32)
    other.handle_ = kInvalidHandle;
#elif defined(__unix__) || defined(__APPLE__)
    other.descriptor_ = -1;
#else
    other.native_handle_ = -1;
#endif
}

SecureFile& SecureFile::operator=(SecureFile&& other) noexcept {
    if (this != &other) {
        static_cast<void>(close());
        path_ = std::move(other.path_);
        locked_ = other.locked_;
#if defined(_WIN32)
        handle_ = other.handle_;
        volume_serial_ = other.volume_serial_;
        file_index_high_ = other.file_index_high_;
        file_index_low_ = other.file_index_low_;
        other.handle_ = kInvalidHandle;
#elif defined(__unix__) || defined(__APPLE__)
        descriptor_ = other.descriptor_;
        device_ = other.device_;
        inode_ = other.inode_;
        other.descriptor_ = -1;
#else
        native_handle_ = other.native_handle_;
        other.native_handle_ = -1;
#endif
        other.locked_ = false;
    }
    return *this;
}

bool SecureFile::is_open() const noexcept {
#if defined(_WIN32)
    return handle_ != kInvalidHandle && handle_ != INVALID_HANDLE_VALUE;
#elif defined(__unix__) || defined(__APPLE__)
    return descriptor_ >= 0;
#else
    return native_handle_ >= 0;
#endif
}

Status SecureFile::initialize_identity() noexcept {
#if defined(_WIN32)
    if (!is_open()) {
        return Status::io;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (GetFileInformationByHandle(static_cast<HANDLE>(handle_),
                                   &information) == 0) {
        return Status::io;
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
        GetFileType(static_cast<HANDLE>(handle_)) != FILE_TYPE_DISK ||
        information.nNumberOfLinks != 1U) {
        return Status::integrity;
    }
    volume_serial_ = information.dwVolumeSerialNumber;
    file_index_high_ = information.nFileIndexHigh;
    file_index_low_ = information.nFileIndexLow;
    return Status::ok;
#elif defined(__unix__) || defined(__APPLE__)
    if (!is_open()) {
        return Status::io;
    }
    struct stat information {};
    if (::fstat(descriptor_, &information) != 0) {
        return Status::io;
    }
    if (!S_ISREG(information.st_mode) || information.st_nlink != 1) {
        return Status::integrity;
    }
    device_ = static_cast<std::uint64_t>(information.st_dev);
    inode_ = static_cast<std::uint64_t>(information.st_ino);
    return Status::ok;
#else
    return Status::unsupported;
#endif
}

Status SecureFile::create_new(const std::filesystem::path& path,
                              SecureFile& file) noexcept {
    static_cast<void>(file.close());
    if (path.empty()) {
        return Status::invalid_argument;
    }
#if defined(_WIN32)
    const auto handle = CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return Status::io;
    }
    file.handle_ = handle;
#elif defined(__unix__) || defined(__APPLE__)
    int flags = O_CREAT | O_EXCL | O_RDWR;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const auto descriptor = ::open(path.c_str(), flags, 0600);
    if (descriptor < 0) {
        return Status::io;
    }
    file.descriptor_ = descriptor;
#else
    static_cast<void>(path);
    return Status::unsupported;
#endif
    file.path_ = path;
    const auto identity_status = file.initialize_identity();
    if (identity_status != Status::ok) {
        static_cast<void>(file.close());
        return identity_status;
    }
    return Status::ok;
}

Status SecureFile::open_rw(const std::filesystem::path& path,
                           SecureFile& file) noexcept {
    static_cast<void>(file.close());
    if (path.empty()) {
        return Status::invalid_argument;
    }
#if defined(_WIN32)
    const auto handle = CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return Status::io;
    }
    file.handle_ = handle;
#elif defined(__unix__) || defined(__APPLE__)
    int flags = O_RDWR;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const auto descriptor = ::open(path.c_str(), flags);
    if (descriptor < 0) {
        return Status::io;
    }
    file.descriptor_ = descriptor;
#else
    static_cast<void>(path);
    return Status::unsupported;
#endif
    file.path_ = path;
    const auto identity_status = file.initialize_identity();
    if (identity_status != Status::ok) {
        static_cast<void>(file.close());
        return identity_status;
    }
    return Status::ok;
}

Status SecureFile::open_read(const std::filesystem::path& path,
                             SecureFile& file) noexcept {
    static_cast<void>(file.close());
    if (path.empty()) {
        return Status::invalid_argument;
    }
#if defined(_WIN32)
    const auto handle = CreateFileW(
        path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return Status::io;
    }
    file.handle_ = handle;
#elif defined(__unix__) || defined(__APPLE__)
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const auto descriptor = ::open(path.c_str(), flags);
    if (descriptor < 0) {
        return Status::io;
    }
    file.descriptor_ = descriptor;
#else
    static_cast<void>(path);
    return Status::unsupported;
#endif
    file.path_ = path;
    const auto identity_status = file.initialize_identity();
    if (identity_status != Status::ok) {
        static_cast<void>(file.close());
        return identity_status;
    }
    return Status::ok;
}

Status SecureFile::write(const std::span<const std::byte> bytes) noexcept {
    if (!is_open()) {
        return Status::invalid_argument;
    }
#if defined(_WIN32)
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0U;
        if (WriteFile(static_cast<HANDLE>(handle_), bytes.data() + offset, chunk,
                      &written, nullptr) == 0 || written != chunk) {
            return Status::io;
        }
        offset += written;
    }
    return Status::ok;
#elif defined(__unix__) || defined(__APPLE__)
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto count = ::write(descriptor_, bytes.data() + offset,
                                   bytes.size() - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return Status::io;
        }
        if (count == 0) {
            return Status::io;
        }
        offset += static_cast<std::size_t>(count);
    }
    return Status::ok;
#else
    static_cast<void>(bytes);
    return Status::unsupported;
#endif
}

Status SecureFile::size(std::uint64_t& value) const noexcept {
    if (!is_open()) {
        return Status::invalid_argument;
    }
#if defined(_WIN32)
    LARGE_INTEGER result{};
    if (GetFileSizeEx(static_cast<HANDLE>(handle_), &result) == 0 ||
        result.QuadPart < 0) {
        return Status::io;
    }
    value = static_cast<std::uint64_t>(result.QuadPart);
    return Status::ok;
#elif defined(__unix__) || defined(__APPLE__)
    struct stat information {};
    if (::fstat(descriptor_, &information) != 0 || information.st_size < 0) {
        return Status::io;
    }
    value = static_cast<std::uint64_t>(information.st_size);
    return Status::ok;
#else
    static_cast<void>(value);
    return Status::unsupported;
#endif
}

Status SecureFile::read_all(std::vector<std::byte>& bytes,
                            const std::uint64_t max_bytes) const noexcept {
    std::uint64_t file_size = 0U;
    const auto size_status = size(file_size);
    if (size_status != Status::ok) {
        return size_status;
    }
    if (file_size > max_bytes ||
        file_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return Status::resource_limit;
    }
    try {
        bytes.resize(static_cast<std::size_t>(file_size));
#if defined(_WIN32)
        LARGE_INTEGER saved{};
        if (SetFilePointerEx(static_cast<HANDLE>(handle_), {}, &saved,
                             FILE_CURRENT) == 0 ||
            SetFilePointerEx(static_cast<HANDLE>(handle_), {}, nullptr,
                             FILE_BEGIN) == 0) {
            return Status::io;
        }
        std::size_t offset = 0U;
        while (offset < bytes.size()) {
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
                bytes.size() - offset,
                static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
            DWORD read = 0U;
            if (ReadFile(static_cast<HANDLE>(handle_), bytes.data() + offset,
                         chunk,
                         &read, nullptr) == 0 || read == 0U) {
                return Status::io;
            }
            offset += read;
        }
        if (SetFilePointerEx(static_cast<HANDLE>(handle_), saved, nullptr,
                             FILE_BEGIN) == 0) {
            return Status::io;
        }
#elif defined(__unix__) || defined(__APPLE__)
        std::size_t offset = 0U;
        while (offset < bytes.size()) {
            const auto count = ::pread(descriptor_, bytes.data() + offset,
                                       bytes.size() - offset,
                                       static_cast<off_t>(offset));
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return Status::io;
            }
            if (count == 0) {
                return Status::io;
            }
            offset += static_cast<std::size_t>(count);
        }
#else
        return Status::unsupported;
#endif
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status SecureFile::hash_sha256(const std::uint64_t max_bytes,
                               const std::size_t buffer_bytes,
                               std::uint64_t& object_size,
                               Digest256& digest) const noexcept {
    if (!is_open() || buffer_bytes == 0U || buffer_bytes > (16U << 20U)) {
        return Status::invalid_argument;
    }
    try {
        std::vector<std::byte> buffer(buffer_bytes);
        Sha256 hasher;
        object_size = 0U;
#if defined(_WIN32)
        LARGE_INTEGER saved{};
        if (SetFilePointerEx(static_cast<HANDLE>(handle_), {}, &saved,
                             FILE_CURRENT) == 0 ||
            SetFilePointerEx(static_cast<HANDLE>(handle_), {}, nullptr,
                             FILE_BEGIN) == 0) {
            return Status::io;
        }
        while (true) {
            DWORD read = 0U;
            if (ReadFile(static_cast<HANDLE>(handle_), buffer.data(),
                         static_cast<DWORD>(buffer.size()), &read, nullptr) == 0) {
                return Status::io;
            }
            if (read == 0U) {
                break;
            }
            if (checked_add_exceeds(object_size, read, max_bytes) ||
                hasher.update(std::span<const std::byte>(buffer.data(), read)) !=
                    Status::ok) {
                return checked_add_exceeds(object_size, read, max_bytes)
                           ? Status::resource_limit
                           : Status::crypto;
            }
            object_size += read;
        }
        if (SetFilePointerEx(static_cast<HANDLE>(handle_), saved, nullptr,
                             FILE_BEGIN) == 0) {
            return Status::io;
        }
#elif defined(__unix__) || defined(__APPLE__)
        std::uint64_t offset = 0U;
        while (true) {
            const auto count = ::pread(descriptor_, buffer.data(), buffer.size(),
                                       static_cast<off_t>(offset));
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return Status::io;
            }
            if (count == 0) {
                break;
            }
            const auto bytes_read = static_cast<std::size_t>(count);
            if (checked_add_exceeds(object_size, bytes_read, max_bytes) ||
                hasher.update(std::span<const std::byte>(buffer.data(), bytes_read)) !=
                    Status::ok) {
                return checked_add_exceeds(object_size, bytes_read, max_bytes)
                           ? Status::resource_limit
                           : Status::crypto;
            }
            object_size += static_cast<std::uint64_t>(bytes_read);
            offset += static_cast<std::uint64_t>(bytes_read);
        }
#else
        return Status::unsupported;
#endif
        return hasher.finalize(digest);
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status SecureFile::truncate(const std::uint64_t value) noexcept {
    if (!is_open()) {
        return Status::invalid_argument;
    }
#if defined(_WIN32)
    LARGE_INTEGER offset{};
    offset.QuadPart = static_cast<LONGLONG>(value);
    if (SetFilePointerEx(static_cast<HANDLE>(handle_), offset, nullptr,
                         FILE_BEGIN) == 0 ||
        SetEndOfFile(static_cast<HANDLE>(handle_)) == 0) {
        return Status::io;
    }
    return seek_end();
#elif defined(__unix__) || defined(__APPLE__)
    return ::ftruncate(descriptor_, static_cast<off_t>(value)) == 0
               ? seek_end()
               : Status::io;
#else
    static_cast<void>(value);
    return Status::unsupported;
#endif
}

Status SecureFile::seek_end() noexcept {
    if (!is_open()) {
        return Status::invalid_argument;
    }
#if defined(_WIN32)
    return SetFilePointerEx(static_cast<HANDLE>(handle_), {}, nullptr,
                            FILE_END) != 0
               ? Status::ok
               : Status::io;
#elif defined(__unix__) || defined(__APPLE__)
    return ::lseek(descriptor_, 0, SEEK_END) >= 0 ? Status::ok : Status::io;
#else
    return Status::unsupported;
#endif
}

Status SecureFile::synchronize() noexcept {
    if (!is_open()) {
        return Status::invalid_argument;
    }
#if defined(_WIN32)
    return FlushFileBuffers(static_cast<HANDLE>(handle_)) != 0 ? Status::ok
                                                               : Status::io;
#elif defined(__unix__) || defined(__APPLE__)
    return ::fsync(descriptor_) == 0 ? Status::ok : Status::io;
#else
    return Status::unsupported;
#endif
}

Status SecureFile::lock_exclusive() noexcept {
    if (!is_open()) {
        return Status::invalid_argument;
    }
    if (locked_) {
        return Status::ok;
    }
#if defined(_WIN32)
    OVERLAPPED overlapped{};
    if (LockFileEx(static_cast<HANDLE>(handle_), LOCKFILE_EXCLUSIVE_LOCK |
                       LOCKFILE_FAIL_IMMEDIATELY,
                   0U, 1U, 0U, &overlapped) == 0) {
        return Status::io;
    }
#elif defined(__unix__) || defined(__APPLE__)
    if (::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
        return Status::io;
    }
#else
    return Status::unsupported;
#endif
    locked_ = true;
    return Status::ok;
}

Status SecureFile::close() noexcept {
    if (!is_open()) {
        locked_ = false;
        return Status::ok;
    }
#if defined(_WIN32)
    const auto closed = CloseHandle(static_cast<HANDLE>(handle_)) != 0;
    handle_ = kInvalidHandle;
#elif defined(__unix__) || defined(__APPLE__)
    const auto closed = ::close(descriptor_) == 0;
    descriptor_ = -1;
#else
    const auto closed = true;
    native_handle_ = -1;
#endif
    locked_ = false;
    return closed ? Status::ok : Status::io;
}

bool SecureFile::path_matches_identity() const noexcept {
#if defined(_WIN32)
    const auto handle = CreateFileW(
        path_.c_str(), 0U,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const auto read = GetFileInformationByHandle(handle, &information) != 0;
    const auto matches = read &&
                         (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U &&
                         information.dwVolumeSerialNumber == volume_serial_ &&
                         information.nFileIndexHigh == file_index_high_ &&
                         information.nFileIndexLow == file_index_low_;
    CloseHandle(handle);
    return matches;
#elif defined(__unix__) || defined(__APPLE__)
    struct stat information {};
    if (::lstat(path_.c_str(), &information) != 0 ||
        !S_ISREG(information.st_mode)) {
        return false;
    }
    return static_cast<std::uint64_t>(information.st_dev) == device_ &&
           static_cast<std::uint64_t>(information.st_ino) == inode_ &&
           information.st_nlink == 1;
#else
    return false;
#endif
}

Status SecureFile::remove_owned_path() noexcept {
    if (path_.empty() || is_open() || !path_matches_identity()) {
        return Status::io;
    }
#if defined(_WIN32)
    return DeleteFileW(path_.c_str()) != 0 ? Status::ok : Status::io;
#elif defined(__unix__) || defined(__APPLE__)
    return ::unlink(path_.c_str()) == 0 ? Status::ok : Status::io;
#else
    return Status::unsupported;
#endif
}

Status promote_no_replace(SecureFile& file,
                          const std::filesystem::path& temporary_path,
                          const std::filesystem::path& final_path) noexcept {
    if (!file.is_open() || temporary_path.empty() || final_path.empty()) {
        return Status::invalid_argument;
    }
    if (!file.path_matches_identity()) {
        return Status::integrity;
    }
#if defined(_WIN32)
    return MoveFileExW(temporary_path.c_str(), final_path.c_str(),
                       MOVEFILE_WRITE_THROUGH) != 0
               ? Status::ok
               : Status::io;
#elif defined(__unix__) || defined(__APPLE__)
    bool linked = false;
#if defined(AT_EMPTY_PATH)
    if (::linkat(file.descriptor_, "", AT_FDCWD, final_path.c_str(),
                 AT_EMPTY_PATH) == 0) {
        linked = true;
    } else if (errno != EINVAL && errno != ENOTSUP && errno != EOPNOTSUPP) {
        return Status::io;
    }
#endif
    if (!linked) {
        if (::link(temporary_path.c_str(), final_path.c_str()) != 0) {
            return Status::io;
        }
    }
    if (::unlink(temporary_path.c_str()) != 0) {
        return Status::io;
    }
    return synchronize_directory(final_path);
#else
    static_cast<void>(temporary_path);
    static_cast<void>(final_path);
    return Status::unsupported;
#endif
}

}  // namespace glyph::detail
