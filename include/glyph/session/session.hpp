#pragma once

#include "glyph/crypto/sha256.hpp"
#include "glyph/manifest/manifest.hpp"
#include "glyph/transport/object_io.hpp"
#include "glyph/transport/resume_journal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace glyph::session {

enum class SenderState : std::uint8_t {
    idle = 0,
    hashing,
    manifest_ready,
    bootstrap,
    transmitting,
    final_repeat,
    done,
    cancelled,
    invalid_manifest,
    resource_limit,
    integrity_failure,
    storage_failure,
};

enum class ReceiverState : std::uint8_t {
    idle = 0,
    searching,
    surface_found,
    calibrating,
    manifest,
    receiving,
    paused,
    recovering,
    verifying,
    complete,
    cancelled,
    unsupported_profile,
    invalid_manifest,
    resource_limit,
    integrity_failure,
    storage_failure,
    camera_failure,
};

struct SenderConfig {
    std::array<std::uint8_t, 16> transfer_id{};
    std::string display_name;
    std::optional<std::string> media_type;
    std::uint64_t block_size = 64U << 10U;
    std::uint64_t shard_size = 16U << 10U;
    std::string fec_profile = "RS32+8";
    ObjectLimits object_limits{};
};

struct LogicalBlock {
    std::uint32_t block_id = 0U;
    std::vector<std::byte> bytes;
};

class SenderSession final {
  public:
    SenderSession() = default;

    [[nodiscard]] static Status create(std::span<const std::byte> object,
                                       const SenderConfig& config,
                                       SenderSession& session);

    [[nodiscard]] static Status open_file(
        const std::filesystem::path& source_path,
        const SenderConfig& config,
        SenderSession& session);

    [[nodiscard]] Status prepare();
    [[nodiscard]] Status begin_bootstrap();
    [[nodiscard]] Status next_block(LogicalBlock& block, bool& available);
    [[nodiscard]] Status complete_final_repeat();
    void cancel() noexcept;

    [[nodiscard]] SenderState state() const noexcept { return state_; }
    [[nodiscard]] const Manifest& manifest() const noexcept { return manifest_; }
    [[nodiscard]] std::span<const std::byte> manifest_bytes() const noexcept {
        return encoded_manifest_;
    }
    [[nodiscard]] std::uint64_t bytes_emitted() const noexcept {
        return bytes_emitted_;
    }

  private:
    SenderConfig config_{};
    std::vector<std::byte> object_;
    Manifest manifest_{};
    std::vector<std::byte> encoded_manifest_;
    std::filesystem::path source_path_;
    std::ifstream source_stream_;
    Digest256 source_sha256_{};
    Sha256 source_emitted_hasher_{};
    Digest256 source_emitted_sha256_{};
    std::uint64_t source_size_ = 0U;
    bool file_source_ = false;
    bool source_emitted_digest_finalized_ = false;
    SenderState state_ = SenderState::idle;
    std::uint64_t next_offset_ = 0U;
    std::uint32_t next_block_id_ = 0U;
    std::uint64_t bytes_emitted_ = 0U;
};

struct ReceiverConfig {
    std::filesystem::path output_root;
    std::filesystem::path journal_path;
    std::string supported_fec_profile = "RS32+8";
    ObjectLimits object_limits{};
    ResumeJournalLimits journal_limits{};
};

class ReceiverSession final {
  public:
    ReceiverSession() = default;
    ~ReceiverSession();

    [[nodiscard]] static Status create(const ReceiverConfig& config,
                                       ReceiverSession& session);

    [[nodiscard]] Status begin_search();
    [[nodiscard]] Status notify_surface_found();
    [[nodiscard]] Status notify_calibrated();
    [[nodiscard]] Status submit_manifest(std::span<const std::byte> encoded);
    [[nodiscard]] Status submit_block(std::uint32_t block_id,
                                      std::span<const std::byte> bytes);
    [[nodiscard]] Status pause();
    [[nodiscard]] Status finalize();
    void cancel() noexcept;

    [[nodiscard]] ReceiverState state() const noexcept { return state_; }
    [[nodiscard]] const Manifest& manifest() const noexcept { return manifest_; }
    [[nodiscard]] std::uint64_t bytes_received() const noexcept {
        return bytes_received_;
    }
    [[nodiscard]] const std::filesystem::path& final_path() const noexcept {
        return final_path_;
    }

    [[nodiscard]] Status snapshot_journal(ResumeJournalState& state) const;

  private:
    ReceiverConfig config_{};
    Manifest manifest_{};
    std::vector<std::byte> encoded_manifest_;
    AtomicObjectWriter writer_{};
    ResumeJournal journal_{};
    ReceiverState state_ = ReceiverState::idle;
    std::uint32_t next_block_id_ = 0U;
    std::uint64_t bytes_received_ = 0U;
    std::filesystem::path final_path_;
    bool writer_open_ = false;
    bool journal_open_ = false;
};

}  // namespace glyph::session
