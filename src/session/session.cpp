#include "glyph/session/session.hpp"

#include "glyph/crypto/sha256.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <system_error>
#include <utility>

namespace glyph::session {
namespace {

bool is_zero_transfer_id(const std::array<std::uint8_t, 16>& value) noexcept {
    return std::all_of(value.begin(), value.end(), [](const auto byte) {
        return byte == 0U;
    });
}

bool checked_block_count(const std::uint64_t object_size,
                         const std::uint64_t block_size,
                         std::uint64_t& count) noexcept {
    if (block_size == 0U) {
        return false;
    }
    if (object_size == 0U) {
        count = 0U;
        return true;
    }
    if (object_size > std::numeric_limits<std::uint64_t>::max() -
                          (block_size - 1U)) {
        return false;
    }
    count = (object_size + block_size - 1U) / block_size;
    return count <= std::numeric_limits<std::uint32_t>::max();
}

bool checked_resume_position(const Manifest& manifest,
                             const ResumeJournalState& journal_state,
                             std::uint32_t& next_block_id,
                             std::uint64_t& bytes_received) noexcept {
    next_block_id = 0U;
    bytes_received = 0U;
    if (journal_state.verified_blocks.empty()) {
        return true;
    }
    if (journal_state.verified_blocks.size() != 1U ||
        journal_state.verified_blocks.front().first_block_id != 0U ||
        journal_state.verified_blocks.front().block_count == 0U) {
        return false;
    }

    std::uint64_t total_blocks = 0U;
    if (!checked_block_count(manifest.object_size, manifest.block_size,
                             total_blocks)) {
        return false;
    }
    const auto verified_blocks =
        journal_state.verified_blocks.front().block_count;
    if (static_cast<std::uint64_t>(verified_blocks) > total_blocks ||
        verified_blocks >
            std::numeric_limits<std::uint64_t>::max() / manifest.block_size) {
        return false;
    }

    next_block_id = verified_blocks;
    bytes_received = std::min(
        manifest.object_size,
        static_cast<std::uint64_t>(verified_blocks) * manifest.block_size);
    return true;
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

bool valid_sender_config(const SenderConfig& config) noexcept {
    return !is_zero_transfer_id(config.transfer_id) &&
           !config.display_name.empty() && config.block_size != 0U &&
           config.shard_size != 0U && !config.fec_profile.empty() &&
           config.object_limits.max_object_size != 0U;
}

void set_sender_failure(SenderState& state, const Status status) noexcept {
    if (status == Status::resource_limit) {
        state = SenderState::resource_limit;
    } else if (status == Status::integrity) {
        state = SenderState::integrity_failure;
    } else if (status == Status::io) {
        state = SenderState::storage_failure;
    } else {
        state = SenderState::invalid_manifest;
    }
}

void set_receiver_failure(ReceiverState& state, const Status status) noexcept {
    if (status == Status::resource_limit) {
        state = ReceiverState::resource_limit;
    } else if (status == Status::integrity) {
        state = ReceiverState::integrity_failure;
    } else {
        state = ReceiverState::storage_failure;
    }
}

}  // namespace

ReceiverSession::~ReceiverSession() {
    if (writer_open_) {
        if (writer_.suspend() != Status::ok) {
            writer_.abort();
        }
        writer_open_ = false;
    }
    if (journal_open_) {
        journal_.close();
        journal_open_ = false;
    }
}

Status SenderSession::create(const std::span<const std::byte> object,
                             const SenderConfig& config,
                             SenderSession& session) {
    session.cancel();
    if (!valid_sender_config(config) ||
        object.size() > config.object_limits.max_object_size ||
        object.size() > std::numeric_limits<std::size_t>::max()) {
        return Status::invalid_argument;
    }

    try {
        session.config_ = config;
        session.object_.assign(object.begin(), object.end());
        session.state_ = SenderState::idle;
        session.next_offset_ = 0U;
        session.next_block_id_ = 0U;
        session.bytes_emitted_ = 0U;
        return Status::ok;
    } catch (const std::bad_alloc&) {
        session.cancel();
        return Status::resource_limit;
    }
}

Status SenderSession::open_file(const std::filesystem::path& source_path,
                                const SenderConfig& config,
                                SenderSession& session) {
    session.cancel();
    if (!valid_sender_config(config) || source_path.empty() ||
        config.object_limits.io_buffer_bytes == 0U ||
        config.object_limits.io_buffer_bytes > (16U << 20U)) {
        return Status::invalid_argument;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(source_path, error) || error) {
        return Status::io;
    }
    try {
        session.config_ = config;
        session.source_path_ = source_path;
        session.file_source_ = true;
        session.source_size_ = 0U;
        session.source_sha256_ = {};
        session.state_ = SenderState::idle;
        session.next_offset_ = 0U;
        session.next_block_id_ = 0U;
        session.bytes_emitted_ = 0U;
        return Status::ok;
    } catch (const std::bad_alloc&) {
        session.cancel();
        return Status::resource_limit;
    }
}

Status SenderSession::prepare() {
    if (state_ != SenderState::idle) {
        return Status::invalid_argument;
    }
    state_ = SenderState::hashing;

    std::uint64_t object_size = object_.size();
    Digest256 object_sha256{};
    if (file_source_) {
        const auto hashed = hash_object_file(source_path_, config_.object_limits);
        if (hashed.status != Status::ok) {
            set_sender_failure(state_, hashed.status);
            return hashed.status;
        }
        object_size = hashed.object_size;
        object_sha256 = hashed.sha256;
        source_size_ = object_size;
        source_sha256_ = object_sha256;
        source_emitted_hasher_ = Sha256{};
        source_emitted_sha256_ = {};
        source_emitted_digest_finalized_ = false;
        source_stream_.open(source_path_, std::ios::binary);
        if (!source_stream_.is_open()) {
            state_ = SenderState::storage_failure;
            return Status::io;
        }
    } else {
        object_sha256 = Sha256::hash(object_);
    }

    std::uint64_t block_count = 0U;
    if (!checked_block_count(object_size, config_.block_size, block_count)) {
        state_ = SenderState::resource_limit;
        return Status::resource_limit;
    }

    try {
        manifest_ = Manifest{};
        manifest_.transfer_id = config_.transfer_id;
        manifest_.object_sha256 = object_sha256;
        manifest_.object_size = object_size;
        manifest_.display_name = config_.display_name;
        manifest_.media_type = config_.media_type;
        manifest_.block_size = config_.block_size;
        manifest_.shard_size = config_.shard_size;
        manifest_.fec_profile = config_.fec_profile;
        const auto validation = validate_manifest(manifest_);
        if (validation.status != Status::ok) {
            set_sender_failure(state_, validation.status);
            return validation.status;
        }
        const auto encoded = encode_manifest(manifest_);
        if (encoded.status != Status::ok) {
            set_sender_failure(state_, encoded.status);
            return encoded.status;
        }
        encoded_manifest_ = encoded.bytes;
        state_ = SenderState::manifest_ready;
        return Status::ok;
    } catch (const std::bad_alloc&) {
        state_ = SenderState::resource_limit;
        return Status::resource_limit;
    }
}

Status SenderSession::begin_bootstrap() {
    if (state_ != SenderState::manifest_ready) {
        return Status::invalid_argument;
    }
    state_ = SenderState::bootstrap;
    return Status::ok;
}

Status SenderSession::next_block(LogicalBlock& block, bool& available) {
    available = false;
    if (state_ != SenderState::bootstrap &&
        state_ != SenderState::transmitting) {
        return Status::invalid_argument;
    }
    if (state_ == SenderState::bootstrap) {
        state_ = SenderState::transmitting;
    }
    const auto source_size = file_source_ ? source_size_
                                          : static_cast<std::uint64_t>(object_.size());
    if (next_offset_ >= source_size) {
        if (file_source_) {
            if (!source_emitted_digest_finalized_) {
                const auto finalize_status =
                    source_emitted_hasher_.finalize(source_emitted_sha256_);
                if (finalize_status != Status::ok) {
                    set_sender_failure(state_, finalize_status);
                    return finalize_status;
                }
                source_emitted_digest_finalized_ = true;
            }
            if (bytes_emitted_ != source_size_ ||
                source_emitted_sha256_ != source_sha256_) {
                state_ = SenderState::integrity_failure;
                return Status::integrity;
            }
        }
        state_ = SenderState::final_repeat;
        return Status::ok;
    }

    const auto remaining = source_size - next_offset_;
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
        remaining, config_.block_size));
    try {
        LogicalBlock candidate;
        candidate.block_id = next_block_id_;
        if (file_source_) {
            candidate.bytes.resize(count);
            source_stream_.read(
                reinterpret_cast<char*>(candidate.bytes.data()),
                static_cast<std::streamsize>(count));
            if (source_stream_.gcount() != static_cast<std::streamsize>(count)) {
                const auto status = source_stream_.bad() ? Status::io
                                                         : Status::integrity;
                set_sender_failure(state_, status);
                return status;
            }
            const auto hash_status =
                source_emitted_hasher_.update(candidate.bytes);
            if (hash_status != Status::ok) {
                set_sender_failure(state_, hash_status);
                return hash_status;
            }
        } else {
            candidate.bytes.assign(
                object_.begin() + static_cast<std::ptrdiff_t>(next_offset_),
                object_.begin() +
                    static_cast<std::ptrdiff_t>(next_offset_ + count));
        }
        block = std::move(candidate);
        next_offset_ += count;
        ++next_block_id_;
        bytes_emitted_ += count;
        available = true;
        return Status::ok;
    } catch (const std::bad_alloc&) {
        state_ = SenderState::resource_limit;
        return Status::resource_limit;
    }
}

Status SenderSession::complete_final_repeat() {
    if (state_ != SenderState::final_repeat) {
        return Status::invalid_argument;
    }
    state_ = SenderState::done;
    return Status::ok;
}

void SenderSession::cancel() noexcept {
    if (source_stream_.is_open()) {
        source_stream_.close();
    }
    object_.clear();
    encoded_manifest_.clear();
    source_path_.clear();
    source_sha256_ = {};
    source_emitted_hasher_ = Sha256{};
    source_emitted_sha256_ = {};
    source_size_ = 0U;
    file_source_ = false;
    source_emitted_digest_finalized_ = false;
    manifest_ = {};
    config_ = {};
    next_offset_ = 0U;
    next_block_id_ = 0U;
    bytes_emitted_ = 0U;
    state_ = SenderState::cancelled;
}

Status ReceiverSession::create(const ReceiverConfig& config,
                               ReceiverSession& session) {
    session.cancel();
    if (config.output_root.empty() || config.journal_path.empty() ||
        config.supported_fec_profile.empty() ||
        config.object_limits.max_object_size == 0U ||
        config.object_limits.io_buffer_bytes == 0U ||
        config.object_limits.io_buffer_bytes > (16U << 20U)) {
        return Status::invalid_argument;
    }
    session.config_ = config;
    session.state_ = ReceiverState::idle;
    session.writer_open_ = false;
    session.journal_open_ = false;
    return Status::ok;
}

Status ReceiverSession::begin_search() {
    if (state_ != ReceiverState::idle) {
        return Status::invalid_argument;
    }
    state_ = ReceiverState::searching;
    return Status::ok;
}

Status ReceiverSession::notify_surface_found() {
    if (state_ != ReceiverState::searching) {
        return Status::invalid_argument;
    }
    state_ = ReceiverState::surface_found;
    return Status::ok;
}

Status ReceiverSession::notify_calibrated() {
    if (state_ != ReceiverState::surface_found) {
        return Status::invalid_argument;
    }
    state_ = ReceiverState::calibrating;
    return Status::ok;
}

Status ReceiverSession::submit_manifest(
    const std::span<const std::byte> encoded) {
    if (state_ != ReceiverState::calibrating || encoded.empty()) {
        return Status::invalid_argument;
    }
    state_ = ReceiverState::manifest;
    ManifestLimits manifest_limits;
    manifest_limits.max_object_size = config_.object_limits.max_object_size;
    const auto decoded = decode_manifest(encoded, manifest_limits);
    if (decoded.status != Status::ok) {
        state_ = decoded.status == Status::resource_limit
                      ? ReceiverState::resource_limit
                      : ReceiverState::invalid_manifest;
        return decoded.status;
    }
    if (decoded.manifest.fec_profile != config_.supported_fec_profile) {
        state_ = ReceiverState::unsupported_profile;
        return Status::unsupported;
    }

    try {
        const auto manifest_digest = Sha256::hash(encoded);
        ResumeJournalIdentity identity;
        identity.transfer_id = decoded.manifest.transfer_id;
        identity.object_sha256 = decoded.manifest.object_sha256;
        identity.manifest_sha256 = manifest_digest;
        identity.object_size = decoded.manifest.object_size;

        ResumeJournal candidate_journal;
        const auto journal_status = ResumeJournal::open(
            config_.journal_path, identity, config_.journal_limits,
            candidate_journal);
        if (journal_status != Status::ok) {
            set_receiver_failure(state_, journal_status);
            return journal_status;
        }
        ResumeJournalState existing_state;
        const auto snapshot_status = candidate_journal.snapshot(existing_state);
        if (snapshot_status != Status::ok) {
            candidate_journal.close();
            set_receiver_failure(state_, snapshot_status);
            return snapshot_status;
        }

        std::uint32_t resume_block_id = 0U;
        std::uint64_t resume_size = 0U;
        if (!checked_resume_position(decoded.manifest, existing_state,
                                      resume_block_id, resume_size)) {
            candidate_journal.close();
            state_ = ReceiverState::integrity_failure;
            return Status::integrity;
        }

        const auto candidate_final_path =
            config_.output_root /
            sanitize_display_name(decoded.manifest.display_name);
        std::error_code final_error;
        const auto final_is_symlink =
            is_symlink_entry(candidate_final_path, final_error);
        if (final_error) {
            candidate_journal.close();
            state_ = ReceiverState::storage_failure;
            return Status::io;
        }
        if (final_is_symlink) {
            candidate_journal.close();
            state_ = ReceiverState::integrity_failure;
            return Status::integrity;
        }
        const auto final_exists =
            std::filesystem::exists(candidate_final_path, final_error);
        if (final_error) {
            candidate_journal.close();
            state_ = ReceiverState::storage_failure;
            return Status::io;
        }
        if (existing_state.completed && !final_exists) {
            candidate_journal.close();
            state_ = ReceiverState::integrity_failure;
            return Status::integrity;
        }
        if (existing_state.completed &&
            resume_size != decoded.manifest.object_size) {
            candidate_journal.close();
            state_ = ReceiverState::integrity_failure;
            return Status::integrity;
        }
        if (final_exists &&
            (existing_state.completed ||
             resume_size == decoded.manifest.object_size)) {
            const auto verify_status = verify_object_file(
                candidate_final_path, decoded.manifest.object_size,
                decoded.manifest.object_sha256, config_.object_limits);
            if (verify_status != Status::ok) {
                candidate_journal.close();
                set_receiver_failure(state_, verify_status);
                return verify_status;
            }

            manifest_ = decoded.manifest;
            encoded_manifest_.assign(encoded.begin(), encoded.end());
            journal_ = std::move(candidate_journal);
            writer_open_ = false;
            journal_open_ = true;
            next_block_id_ = resume_block_id;
            bytes_received_ = resume_size;
            final_path_ = candidate_final_path;
            state_ = ReceiverState::complete;
            return Status::ok;
        }

        AtomicObjectWriter candidate_writer;
        const auto writer_status = AtomicObjectWriter::open_resumable(
            config_.output_root, decoded.manifest.display_name, manifest_digest,
            resume_size, config_.object_limits, candidate_writer);
        if (writer_status != Status::ok) {
            candidate_journal.close();
            state_ = writer_status == Status::integrity
                          ? ReceiverState::integrity_failure
                          : ReceiverState::storage_failure;
            return writer_status;
        }

        manifest_ = decoded.manifest;
        encoded_manifest_.assign(encoded.begin(), encoded.end());
        writer_ = std::move(candidate_writer);
        journal_ = std::move(candidate_journal);
        writer_open_ = true;
        journal_open_ = true;
        next_block_id_ = resume_block_id;
        bytes_received_ = resume_size;
        state_ = ReceiverState::receiving;
        return Status::ok;
    } catch (const std::bad_alloc&) {
        state_ = ReceiverState::resource_limit;
        return Status::resource_limit;
    }
}

Status ReceiverSession::submit_block(const std::uint32_t block_id,
                                     const std::span<const std::byte> bytes) {
    if (state_ != ReceiverState::receiving || !writer_open_ ||
        !journal_open_ || block_id != next_block_id_ ||
        bytes_received_ > manifest_.object_size ||
        bytes.size() > manifest_.block_size ||
        bytes.size() > std::numeric_limits<std::uint64_t>::max() -
                            bytes_received_) {
        return Status::invalid_argument;
    }
    const auto remaining = manifest_.object_size - bytes_received_;
    const auto expected = std::min<std::uint64_t>(manifest_.block_size, remaining);
    if (bytes.size() != expected) {
        return Status::protocol;
    }

    const auto write_status = writer_.write(bytes);
    if (write_status != Status::ok) {
        state_ = write_status == Status::resource_limit
                      ? ReceiverState::resource_limit
                      : ReceiverState::storage_failure;
        writer_.abort();
        journal_.close();
        writer_open_ = false;
        journal_open_ = false;
        return write_status;
    }
    const auto checkpoint_status = writer_.durable_checkpoint();
    if (checkpoint_status != Status::ok) {
        state_ = ReceiverState::storage_failure;
        writer_.abort();
        journal_.close();
        writer_open_ = false;
        journal_open_ = false;
        return checkpoint_status;
    }
    const auto journal_status = journal_.append_verified_block(block_id);
    if (journal_status != Status::ok) {
        state_ = journal_status == Status::resource_limit
                      ? ReceiverState::resource_limit
                      : ReceiverState::storage_failure;
        writer_.abort();
        journal_.close();
        writer_open_ = false;
        journal_open_ = false;
        return journal_status;
    }
    bytes_received_ += bytes.size();
    ++next_block_id_;
    return Status::ok;
}

Status ReceiverSession::pause() {
    if (state_ != ReceiverState::receiving || !writer_open_ ||
        !journal_open_) {
        return Status::invalid_argument;
    }
    const auto checkpoint_status = writer_.durable_checkpoint();
    if (checkpoint_status != Status::ok) {
        state_ = ReceiverState::storage_failure;
        writer_.abort();
        journal_.close();
        writer_open_ = false;
        journal_open_ = false;
        return checkpoint_status;
    }
    const auto suspend_status = writer_.suspend();
    if (suspend_status != Status::ok) {
        state_ = ReceiverState::storage_failure;
        journal_.close();
        writer_open_ = false;
        journal_open_ = false;
        return suspend_status;
    }
    journal_.close();
    writer_open_ = false;
    journal_open_ = false;
    state_ = ReceiverState::paused;
    return Status::ok;
}

Status ReceiverSession::finalize() {
    if (state_ != ReceiverState::receiving || !writer_open_ ||
        !journal_open_ || bytes_received_ != manifest_.object_size) {
        return Status::invalid_argument;
    }
    state_ = ReceiverState::recovering;
    state_ = ReceiverState::verifying;
    const auto final_path = writer_.final_path();
    const auto finalize_status = writer_.finalize(manifest_.object_size,
                                                  manifest_.object_sha256);
    if (finalize_status != Status::ok) {
        state_ = finalize_status == Status::integrity
                      ? ReceiverState::integrity_failure
                      : ReceiverState::storage_failure;
        journal_.close();
        writer_open_ = false;
        journal_open_ = false;
        return finalize_status;
    }

    const auto completion_status = journal_.append_complete();
    if (completion_status != Status::ok) {
        set_receiver_failure(state_, completion_status);
        journal_.close();
        writer_open_ = false;
        journal_open_ = false;
        return completion_status;
    }

    final_path_ = final_path;
    journal_.close();
    writer_open_ = false;
    journal_open_ = false;
    state_ = ReceiverState::complete;
    return Status::ok;
}

void ReceiverSession::cancel() noexcept {
    const auto discard_journal = journal_open_ || !encoded_manifest_.empty();
    if (writer_open_ || !writer_.temporary_path().empty()) {
        writer_.abort();
    }
    if (journal_open_) {
        journal_.close();
    }
    if (discard_journal) {
        std::error_code error;
        std::filesystem::remove(config_.journal_path, error);
    }
    config_ = {};
    manifest_ = {};
    encoded_manifest_.clear();
    state_ = ReceiverState::cancelled;
    next_block_id_ = 0U;
    bytes_received_ = 0U;
    final_path_.clear();
    writer_open_ = false;
    journal_open_ = false;
}

Status ReceiverSession::snapshot_journal(ResumeJournalState& state) const {
    if (!journal_open_) {
        return Status::invalid_argument;
    }
    return journal_.snapshot(state);
}

}  // namespace glyph::session
