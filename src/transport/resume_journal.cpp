#include "glyph/transport/resume_journal.hpp"

#include "glyph/frame/crc32c.hpp"
#include "secure_file.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <new>
#include <span>
#include <utility>

namespace glyph {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'G', 'L', 'J', '2'};
constexpr std::uint16_t kVersion = 2U;
constexpr std::uint16_t kHeaderBytes = 100U;
constexpr std::uint32_t kMaxRecordPayloadBytes = 4U;
constexpr std::uint8_t kShardRecord = 1U;
constexpr std::uint8_t kBlockRecord = 2U;
constexpr std::uint8_t kCompleteRecord = 3U;

constexpr std::size_t kHeaderCrcOffset = 96U;

void append_u16(std::vector<std::byte>& output, const std::uint16_t value) {
    output.push_back(static_cast<std::byte>(value >> 8U));
    output.push_back(static_cast<std::byte>(value));
}

void append_u32(std::vector<std::byte>& output, const std::uint32_t value) {
    output.push_back(static_cast<std::byte>(value >> 24U));
    output.push_back(static_cast<std::byte>(value >> 16U));
    output.push_back(static_cast<std::byte>(value >> 8U));
    output.push_back(static_cast<std::byte>(value));
}

void append_u64(std::vector<std::byte>& output, const std::uint64_t value) {
    for (unsigned int index = 0U; index < 8U; ++index) {
        output.push_back(static_cast<std::byte>(
            value >> (56U - (index * 8U))));
    }
}

std::uint16_t read_u16(const std::span<const std::byte> bytes,
                       const std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset]))
         << 8U) |
        std::to_integer<std::uint8_t>(bytes[offset + 1U]));
}

std::uint32_t read_u32(const std::span<const std::byte> bytes,
                       const std::size_t offset) {
    return (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[offset]))
            << 24U) |
           (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[offset + 1U]))
            << 16U) |
           (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[offset + 2U]))
            << 8U) |
           std::to_integer<std::uint8_t>(bytes[offset + 3U]);
}

std::uint64_t read_u64(const std::span<const std::byte> bytes,
                       const std::size_t offset) {
    std::uint64_t value = 0U;
    for (unsigned int index = 0U; index < 8U; ++index) {
        value = (value << 8U) |
                std::to_integer<std::uint8_t>(bytes[offset + index]);
    }
    return value;
}

bool is_zero(const std::array<std::uint8_t, 16>& value) {
    return std::all_of(value.begin(), value.end(), [](const auto byte) {
        return byte == 0U;
    });
}

bool checked_add(const std::uint64_t left,
                 const std::uint64_t right,
                 std::uint64_t& result) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

bool contains_shard(const std::vector<ResumeShardReceipt>& receipts,
                    const ResumeShardReceipt receipt) {
    return std::find(receipts.begin(), receipts.end(), receipt) !=
           receipts.end();
}

std::uint64_t counted_blocks(
    const std::vector<ResumeBlockRange>& ranges) {
    std::uint64_t count = 0U;
    for (const auto& range : ranges) {
        count += range.block_count;
    }
    return count;
}

bool contains_block(const std::vector<ResumeBlockRange>& ranges,
                    const std::uint32_t block_id) {
    for (const auto& range : ranges) {
        if (block_id < range.first_block_id) {
            return false;
        }
        const auto last = static_cast<std::uint64_t>(range.first_block_id) +
                          range.block_count;
        if (static_cast<std::uint64_t>(block_id) < last) {
            return true;
        }
    }
    return false;
}

Status insert_block(std::vector<ResumeBlockRange>& ranges,
                    const std::uint32_t block_id,
                    bool& inserted) {
    inserted = false;
    for (std::size_t index = 0U; index < ranges.size(); ++index) {
        auto& range = ranges[index];
        const auto range_end = static_cast<std::uint64_t>(range.first_block_id) +
                               range.block_count;
        if (static_cast<std::uint64_t>(block_id) >= range.first_block_id &&
            static_cast<std::uint64_t>(block_id) < range_end) {
            return Status::ok;
        }
        if (block_id < range.first_block_id) {
            const auto joins_next =
                static_cast<std::uint64_t>(block_id) + 1U ==
                range.first_block_id;
            if (index > 0U) {
                auto& previous = ranges[index - 1U];
                const auto previous_end =
                    static_cast<std::uint64_t>(previous.first_block_id) +
                    previous.block_count;
                if (previous_end == block_id) {
                    ++previous.block_count;
                    if (joins_next) {
                        previous.block_count += range.block_count;
                        ranges.erase(ranges.begin() +
                                     static_cast<std::ptrdiff_t>(index));
                    }
                    inserted = true;
                    return Status::ok;
                }
            }
            if (joins_next) {
                range.first_block_id = block_id;
                ++range.block_count;
            } else {
                ranges.insert(ranges.begin() + static_cast<std::ptrdiff_t>(index),
                              ResumeBlockRange{block_id, 1U});
            }
            inserted = true;
            return Status::ok;
        }
        if (range_end == block_id) {
            ++range.block_count;
            if (index + 1U < ranges.size() &&
                static_cast<std::uint64_t>(block_id) + 1U ==
                    ranges[index + 1U].first_block_id) {
                range.block_count += ranges[index + 1U].block_count;
                ranges.erase(ranges.begin() +
                             static_cast<std::ptrdiff_t>(index + 1U));
            }
            inserted = true;
            return Status::ok;
        }
    }

    ranges.push_back(ResumeBlockRange{block_id, 1U});
    inserted = true;
    return Status::ok;
}

std::vector<std::byte> encode_header(const ResumeJournalIdentity& identity) {
    std::vector<std::byte> header;
    header.reserve(kHeaderBytes);
    for (const auto byte : kMagic) {
        header.push_back(static_cast<std::byte>(byte));
    }
    append_u16(header, kVersion);
    append_u16(header, kHeaderBytes);
    for (const auto byte : identity.transfer_id) {
        header.push_back(static_cast<std::byte>(byte));
    }
    for (const auto byte : identity.object_sha256.bytes) {
        header.push_back(static_cast<std::byte>(byte));
    }
    for (const auto byte : identity.manifest_sha256.bytes) {
        header.push_back(static_cast<std::byte>(byte));
    }
    append_u64(header, identity.object_size);
    append_u32(header, crc32c(header));
    return header;
}

Status parse_header(const std::span<const std::byte> bytes,
                    const ResumeJournalIdentity& expected,
                    ResumeJournalIdentity& actual) {
    if (bytes.size() < kHeaderBytes) {
        return Status::integrity;
    }
    for (std::size_t index = 0U; index < kMagic.size(); ++index) {
        if (std::to_integer<std::uint8_t>(bytes[index]) != kMagic[index]) {
            return Status::protocol;
        }
    }
    if (read_u16(bytes, 4U) != kVersion || read_u16(bytes, 6U) != kHeaderBytes) {
        return Status::protocol;
    }
    if (read_u32(bytes, kHeaderCrcOffset) != crc32c(bytes.first(kHeaderCrcOffset))) {
        return Status::integrity;
    }

    actual.transfer_id = {};
    for (std::size_t index = 0U; index < actual.transfer_id.size(); ++index) {
        actual.transfer_id[index] =
            std::to_integer<std::uint8_t>(bytes[8U + index]);
    }
    for (std::size_t index = 0U; index < actual.object_sha256.bytes.size();
         ++index) {
        actual.object_sha256.bytes[index] =
            std::to_integer<std::uint8_t>(bytes[24U + index]);
        actual.manifest_sha256.bytes[index] =
            std::to_integer<std::uint8_t>(bytes[56U + index]);
    }
    actual.object_size = read_u64(bytes, 88U);
    return actual == expected ? Status::ok : Status::integrity;
}

std::vector<std::byte> encode_record(const std::uint8_t type,
                                     const std::span<const std::byte> payload) {
    std::vector<std::byte> record;
    record.reserve(4U + 1U + payload.size() + 4U);
    append_u32(record, static_cast<std::uint32_t>(1U + payload.size()));
    record.push_back(static_cast<std::byte>(type));
    record.insert(record.end(), payload.begin(), payload.end());
    const auto crc = crc32c(std::span<const std::byte>(record).subspan(4U));
    append_u32(record, crc);
    return record;
}

Status apply_record(const std::uint8_t type,
                    const std::span<const std::byte> payload,
                    const ResumeJournalLimits& limits,
                    ResumeJournalState& state) {
    if (type == kCompleteRecord) {
        if (!payload.empty()) {
            return Status::protocol;
        }
        state.completed = true;
        return Status::ok;
    }
    if (state.completed) {
        return Status::integrity;
    }
    if (type == kShardRecord) {
        if (payload.size() != 4U) {
            return Status::protocol;
        }
        const ResumeShardReceipt receipt{read_u16(payload, 0U),
                                         read_u16(payload, 2U)};
        if (contains_shard(state.shard_receipts, receipt)) {
            return Status::ok;
        }
        if (state.shard_receipts.size() >= limits.max_shard_receipts) {
            return Status::resource_limit;
        }
        state.shard_receipts.push_back(receipt);
        return Status::ok;
    }
    if (type == kBlockRecord) {
        if (payload.size() != 4U) {
            return Status::protocol;
        }
        bool inserted = false;
        const auto status = insert_block(
            state.verified_blocks, read_u32(payload, 0U), inserted);
        if (status != Status::ok) {
            return status;
        }
        if (inserted && counted_blocks(state.verified_blocks) >
                             limits.max_verified_blocks) {
            return Status::resource_limit;
        }
        return Status::ok;
    }
    return Status::protocol;
}

Status read_existing(detail::SecureFile& file,
                     const ResumeJournalIdentity& identity,
                     const ResumeJournalLimits& limits,
                     ResumeJournalState& state,
                     std::uint64_t& valid_bytes,
                     std::uint64_t& file_bytes) {
    const auto size_status = file.size(file_bytes);
    if (size_status != Status::ok) {
        return size_status;
    }
    if (file_bytes < kHeaderBytes) {
        return Status::integrity;
    }
    if (file_bytes > limits.max_journal_bytes) {
        return Status::resource_limit;
    }

    try {
        std::vector<std::byte> bytes;
        const auto read_status = file.read_all(bytes, limits.max_journal_bytes);
        if (read_status != Status::ok) {
            return read_status;
        }

        ResumeJournalIdentity actual;
        const auto header_status = parse_header(bytes, identity, actual);
        if (header_status != Status::ok) {
            return header_status;
        }
        state.identity = actual;

        valid_bytes = kHeaderBytes;
        while (valid_bytes < file_bytes) {
            const auto remaining = file_bytes - valid_bytes;
            if (remaining < 4U) {
                break;
            }
            const auto offset = static_cast<std::size_t>(valid_bytes);
            const auto payload_length = read_u32(bytes, offset);
            if (payload_length == 0U ||
                payload_length > 1U + kMaxRecordPayloadBytes) {
                return Status::protocol;
            }
            const auto record_bytes = static_cast<std::uint64_t>(4U) +
                                      payload_length + 4U;
            if (remaining < record_bytes) {
                break;
            }
            const auto record_span =
                std::span<const std::byte>(bytes).subspan(
                    offset, static_cast<std::size_t>(record_bytes));
            const auto expected_crc =
                read_u32(record_span, 4U + payload_length);
            const auto actual_crc = crc32c(
                record_span.subspan(4U, static_cast<std::size_t>(payload_length)));
            if (expected_crc != actual_crc) {
                return Status::integrity;
            }
            const auto status = apply_record(
                std::to_integer<std::uint8_t>(record_span[4U]),
                record_span.subspan(5U, payload_length - 1U), limits, state);
            if (status != Status::ok) {
                return status;
            }
            valid_bytes += record_bytes;
        }
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status validate_limits(const ResumeJournalLimits& limits) {
    if (limits.max_journal_bytes < kHeaderBytes ||
        limits.max_journal_bytes > kMaxResumeJournalBytes ||
        limits.max_shard_receipts == 0U ||
        limits.max_shard_receipts > kMaxResumeShardReceipts ||
        limits.max_verified_blocks == 0U ||
        limits.max_verified_blocks > kMaxResumeVerifiedBlocks) {
        return Status::invalid_argument;
    }
    return Status::ok;
}

}  // namespace

ResumeJournal::~ResumeJournal() {
    close();
}

ResumeJournal::ResumeJournal() noexcept = default;

ResumeJournal::ResumeJournal(ResumeJournal&& other) noexcept
    : file_(std::move(other.file_)),
      path_(std::move(other.path_)),
      limits_(other.limits_),
      state_(std::move(other.state_)),
      journal_bytes_(other.journal_bytes_),
      open_(other.open_) {
    other.path_.clear();
    other.limits_ = {};
    other.state_ = {};
    other.journal_bytes_ = 0U;
    other.open_ = false;
}

ResumeJournal& ResumeJournal::operator=(ResumeJournal&& other) noexcept {
    if (this != &other) {
        close();
        file_ = std::move(other.file_);
        path_ = std::move(other.path_);
        limits_ = other.limits_;
        state_ = std::move(other.state_);
        journal_bytes_ = other.journal_bytes_;
        open_ = other.open_;
        other.path_.clear();
        other.limits_ = {};
        other.state_ = {};
        other.journal_bytes_ = 0U;
        other.open_ = false;
    }
    return *this;
}

Status ResumeJournal::open(const std::filesystem::path& path,
                           const ResumeJournalIdentity& identity,
                           const ResumeJournalLimits& limits,
                           ResumeJournal& journal) {
    journal.close();
    if (validate_limits(limits) != Status::ok || is_zero(identity.transfer_id)) {
        return Status::invalid_argument;
    }
    if (path.empty()) {
        return Status::invalid_argument;
    }

    std::error_code error;
    const auto parent = path.parent_path();
    if (!parent.empty() &&
        (!std::filesystem::is_directory(parent, error) || error)) {
        return Status::io;
    }

    try {
        ResumeJournal candidate;
        candidate.path_ = path;
        candidate.limits_ = limits;
        candidate.state_.identity = identity;

        const bool exists = std::filesystem::exists(path, error);
        if (error) {
            return Status::io;
        }
        candidate.file_ = std::make_unique<detail::SecureFile>();
        if (exists) {
            std::uint64_t valid_bytes = 0U;
            std::uint64_t file_bytes = 0U;
            const auto open_status = detail::SecureFile::open_rw(
                path, *candidate.file_);
            if (open_status != Status::ok) {
                return open_status;
            }
            const auto lock_status = candidate.file_->lock_exclusive();
            if (lock_status != Status::ok) {
                return lock_status;
            }
            const auto status = read_existing(*candidate.file_, identity, limits,
                                              candidate.state_, valid_bytes,
                                              file_bytes);
            if (status != Status::ok) {
                return status;
            }
            if (valid_bytes < file_bytes) {
                const auto truncate_status = candidate.file_->truncate(valid_bytes);
                if (truncate_status != Status::ok) {
                    return truncate_status;
                }
                const auto sync_status = candidate.file_->synchronize();
                if (sync_status != Status::ok) {
                    return sync_status;
                }
            }
            candidate.journal_bytes_ = valid_bytes;
        } else {
            const auto header = encode_header(identity);
            auto create_status = detail::SecureFile::create_new(
                path, *candidate.file_);
            if (create_status != Status::ok) {
                // A concurrent opener may have won the CREATE_NEW race. Open
                // that file through the same secure path and validate it.
                create_status = detail::SecureFile::open_rw(
                    path, *candidate.file_);
                if (create_status != Status::ok) {
                    return create_status;
                }
                const auto lock_status = candidate.file_->lock_exclusive();
                if (lock_status != Status::ok) {
                    return lock_status;
                }
                std::uint64_t valid_bytes = 0U;
                std::uint64_t file_bytes = 0U;
                const auto status = read_existing(
                    *candidate.file_, identity, limits, candidate.state_,
                    valid_bytes, file_bytes);
                if (status != Status::ok) {
                    return status;
                }
                if (valid_bytes < file_bytes) {
                    if (candidate.file_->truncate(valid_bytes) != Status::ok ||
                        candidate.file_->synchronize() != Status::ok) {
                        return Status::io;
                    }
                }
                candidate.journal_bytes_ = valid_bytes;
                if (candidate.file_->seek_end() != Status::ok) {
                    return Status::io;
                }
                candidate.open_ = true;
                journal = std::move(candidate);
                return Status::ok;
            }
            if (candidate.file_->lock_exclusive() != Status::ok ||
                candidate.file_->write(header) != Status::ok ||
                candidate.file_->synchronize() != Status::ok) {
                return Status::io;
            }
            candidate.journal_bytes_ = header.size();
        }
        if (candidate.file_ == nullptr || !candidate.file_->is_open() ||
            candidate.file_->seek_end() != Status::ok) {
            return Status::io;
        }
        candidate.open_ = true;
        journal = std::move(candidate);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status ResumeJournal::append_shard(const std::uint16_t fec_group,
                                   const std::uint16_t shard_index) {
    if (!open_ || state_.completed) {
        return Status::invalid_argument;
    }
    const ResumeShardReceipt receipt{fec_group, shard_index};
    if (contains_shard(state_.shard_receipts, receipt)) {
        return Status::ok;
    }
    if (state_.shard_receipts.size() >= limits_.max_shard_receipts) {
        return Status::resource_limit;
    }

    try {
        auto candidate_receipts = state_.shard_receipts;
        candidate_receipts.push_back(receipt);

        std::vector<std::byte> payload;
        payload.reserve(4U);
        append_u16(payload, fec_group);
        append_u16(payload, shard_index);
        const auto record = encode_record(kShardRecord, payload);
        std::uint64_t next_size = 0U;
        if (!checked_add(journal_bytes_, record.size(), next_size) ||
            next_size > limits_.max_journal_bytes) {
            return Status::resource_limit;
        }
        if (file_ == nullptr || file_->write(record) != Status::ok) {
            return Status::io;
        }
        const auto sync_status = file_->synchronize();
        if (sync_status != Status::ok) {
            return sync_status;
        }
        state_.shard_receipts = std::move(candidate_receipts);
        journal_bytes_ = next_size;
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status ResumeJournal::append_verified_block(const std::uint32_t block_id) {
    if (!open_ || state_.completed) {
        return Status::invalid_argument;
    }
    if (contains_block(state_.verified_blocks, block_id)) {
        return Status::ok;
    }
    if (counted_blocks(state_.verified_blocks) >= limits_.max_verified_blocks) {
        return Status::resource_limit;
    }

    try {
        std::vector<std::byte> payload;
        payload.reserve(4U);
        append_u32(payload, block_id);
        const auto record = encode_record(kBlockRecord, payload);
        std::uint64_t next_size = 0U;
        if (!checked_add(journal_bytes_, record.size(), next_size) ||
            next_size > limits_.max_journal_bytes) {
            return Status::resource_limit;
        }

        auto candidate_ranges = state_.verified_blocks;
        bool inserted = false;
        const auto range_status =
            insert_block(candidate_ranges, block_id, inserted);
        if (range_status != Status::ok || !inserted) {
            return range_status;
        }
        if (file_ == nullptr || file_->write(record) != Status::ok) {
            return Status::io;
        }
        const auto sync_status = file_->synchronize();
        if (sync_status != Status::ok) {
            return sync_status;
        }
        state_.verified_blocks = std::move(candidate_ranges);
        journal_bytes_ = next_size;
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status ResumeJournal::append_complete() {
    if (!open_) {
        return Status::invalid_argument;
    }
    if (state_.completed) {
        return Status::ok;
    }

    try {
        const auto record = encode_record(kCompleteRecord, {});
        std::uint64_t next_size = 0U;
        if (!checked_add(journal_bytes_, record.size(), next_size) ||
            next_size > limits_.max_journal_bytes) {
            return Status::resource_limit;
        }
        if (file_ == nullptr || file_->write(record) != Status::ok) {
            return Status::io;
        }
        const auto sync_status = file_->synchronize();
        if (sync_status != Status::ok) {
            return sync_status;
        }
        state_.completed = true;
        journal_bytes_ = next_size;
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status ResumeJournal::snapshot(ResumeJournalState& state) const {
    if (!open_) {
        return Status::invalid_argument;
    }
    try {
        state = state_;
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

void ResumeJournal::close() noexcept {
    if (file_ != nullptr) {
        static_cast<void>(file_->close());
        file_.reset();
    }
    path_.clear();
    limits_ = {};
    state_ = {};
    journal_bytes_ = 0U;
    open_ = false;
}

}  // namespace glyph
