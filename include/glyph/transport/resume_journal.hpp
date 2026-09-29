#pragma once

#include "glyph/core/status.hpp"
#include "glyph/crypto/sha256.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace glyph {

namespace detail {
class SecureFile;
}

namespace session {
class ReceiverSession;
}

inline constexpr std::uint64_t kMaxResumeJournalBytes = 64ULL << 20U;
inline constexpr std::uint32_t kMaxResumeShardReceipts = 1U << 20U;
inline constexpr std::uint32_t kMaxResumeVerifiedBlocks = 1U << 20U;

struct ResumeJournalIdentity {
    std::array<std::uint8_t, 16> transfer_id{};
    Digest256 object_sha256{};
    Digest256 manifest_sha256{};
    std::uint64_t object_size = 0U;

    friend constexpr bool operator==(const ResumeJournalIdentity&,
                                     const ResumeJournalIdentity&) = default;
};

struct ResumeShardReceipt {
    std::uint16_t fec_group = 0U;
    std::uint16_t shard_index = 0U;

    friend constexpr bool operator==(const ResumeShardReceipt&,
                                     const ResumeShardReceipt&) = default;
};

struct ResumeBlockRange {
    std::uint32_t first_block_id = 0U;
    std::uint32_t block_count = 0U;

    friend constexpr bool operator==(const ResumeBlockRange&,
                                     const ResumeBlockRange&) = default;
};

struct ResumeJournalLimits {
    std::uint64_t max_journal_bytes = kMaxResumeJournalBytes;
    std::uint32_t max_shard_receipts = kMaxResumeShardReceipts;
    std::uint32_t max_verified_blocks = kMaxResumeVerifiedBlocks;
};

struct ResumeJournalState {
    ResumeJournalIdentity identity{};
    std::vector<ResumeShardReceipt> shard_receipts;
    std::vector<ResumeBlockRange> verified_blocks;
    bool completed = false;
};

class ResumeJournal final {
public:
    ResumeJournal() noexcept;
    ~ResumeJournal();

    ResumeJournal(const ResumeJournal&) = delete;
    ResumeJournal& operator=(const ResumeJournal&) = delete;

    ResumeJournal(ResumeJournal&& other) noexcept;
    ResumeJournal& operator=(ResumeJournal&& other) noexcept;

    // Opens an existing journal after identity validation, or creates a new
    // journal with an identity-bound header. A single writer is required.
    [[nodiscard]] static Status open(
        const std::filesystem::path& path,
        const ResumeJournalIdentity& identity,
        const ResumeJournalLimits& limits,
        ResumeJournal& journal);

    // Appends are idempotent. State is updated only after the record is
    // written, flushed, and durably synchronized successfully.
    [[nodiscard]] Status append_shard(std::uint16_t fec_group,
                                      std::uint16_t shard_index);

    [[nodiscard]] Status append_verified_block(std::uint32_t block_id);

    [[nodiscard]] Status snapshot(ResumeJournalState& state) const;

    void close() noexcept;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    [[nodiscard]] std::uint64_t journal_bytes() const noexcept {
        return journal_bytes_;
    }

private:
    friend class session::ReceiverSession;

    // Only ReceiverSession may emit the completion marker, after it has
    // verified and promoted the object. Callers can still observe the marker
    // through snapshot() after reopening the journal.
    [[nodiscard]] Status append_complete();

    std::unique_ptr<detail::SecureFile> file_;
    std::filesystem::path path_;
    ResumeJournalLimits limits_{};
    ResumeJournalState state_{};
    std::uint64_t journal_bytes_ = 0U;
    bool open_ = false;
};

}  // namespace glyph
