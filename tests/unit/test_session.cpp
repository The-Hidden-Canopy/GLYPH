#include "glyph/session/session.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::filesystem::path make_root() {
    const auto base = std::filesystem::temp_directory_path();
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned int attempt = 0U; attempt < 16U; ++attempt) {
        const auto candidate = base / ("glyph-session-" + std::to_string(ticks) +
                                       "-" + std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error) && !error) {
            return candidate;
        }
    }
    return {};
}

std::vector<std::byte> object_bytes() {
    const std::string text = "GLYPH logical session object\n";
    const auto* begin = reinterpret_cast<const std::byte*>(text.data());
    return {begin, begin + text.size()};
}

void append_completion_record(const std::filesystem::path& path) {
    constexpr std::array<std::uint8_t, 9> record{
        0x00U, 0x00U, 0x00U, 0x01U, 0x03U, 0x41U, 0x2dU, 0xa0U, 0xa5U};
    std::ofstream stream(path, std::ios::binary | std::ios::out |
                                  std::ios::app);
    assert(stream.is_open());
    stream.write(reinterpret_cast<const char*>(record.data()),
                 static_cast<std::streamsize>(record.size()));
    assert(stream.good());
}

}  // namespace

int main() {
    using glyph::Status;
    const auto root = make_root();
    assert(!root.empty());
    const auto object = object_bytes();

    glyph::session::SenderConfig sender_config;
    sender_config.transfer_id[0] = 0x31U;
    sender_config.display_name = "session-output.bin";
    sender_config.block_size = 5U;
    sender_config.shard_size = 5U;

    glyph::session::SenderSession sender;
    assert(glyph::session::SenderSession::create(object, sender_config, sender) ==
           Status::ok);
    assert(sender.state() == glyph::session::SenderState::idle);
    assert(sender.prepare() == Status::ok);
    assert(sender.state() == glyph::session::SenderState::manifest_ready);
    assert(sender.begin_bootstrap() == Status::ok);

    glyph::session::ReceiverConfig receiver_config;
    receiver_config.output_root = root;
    receiver_config.journal_path = root / "session.glj";
    glyph::session::LogicalBlock first_block;
    bool first_available = false;
    assert(sender.next_block(first_block, first_available) == Status::ok);
    assert(first_available);
    {
        glyph::session::ReceiverSession receiver;
        assert(glyph::session::ReceiverSession::create(receiver_config, receiver) ==
               Status::ok);
        assert(receiver.begin_search() == Status::ok);
        assert(receiver.notify_surface_found() == Status::ok);
        assert(receiver.notify_calibrated() == Status::ok);
        assert(receiver.submit_manifest(sender.manifest_bytes()) == Status::ok);
        assert(receiver.state() == glyph::session::ReceiverState::receiving);
        assert(receiver.submit_block(first_block.block_id, first_block.bytes) ==
               Status::ok);
    }

    glyph::session::ReceiverSession resumed;
    assert(glyph::session::ReceiverSession::create(receiver_config, resumed) ==
           Status::ok);
    assert(resumed.begin_search() == Status::ok);
    assert(resumed.notify_surface_found() == Status::ok);
    assert(resumed.notify_calibrated() == Status::ok);
    assert(resumed.submit_manifest(sender.manifest_bytes()) == Status::ok);
    assert(resumed.state() == glyph::session::ReceiverState::receiving);
    assert(resumed.bytes_received() == first_block.bytes.size());

    for (;;) {
        glyph::session::LogicalBlock block;
        bool available = false;
        assert(sender.next_block(block, available) == Status::ok);
        if (!available) {
            break;
        }
        assert(resumed.submit_block(block.block_id, block.bytes) == Status::ok);
    }
    assert(sender.state() == glyph::session::SenderState::final_repeat);
    assert(sender.complete_final_repeat() == Status::ok);
    assert(sender.state() == glyph::session::SenderState::done);
    assert(resumed.finalize() == Status::ok);
    assert(resumed.state() == glyph::session::ReceiverState::complete);
    assert(std::filesystem::exists(resumed.final_path()));

    std::ifstream received(resumed.final_path(), std::ios::binary);
    assert(received.is_open());
    const std::vector<char> received_chars{
        std::istreambuf_iterator<char>(received), std::istreambuf_iterator<char>()};
    std::vector<std::byte> received_bytes;
    received_bytes.reserve(received_chars.size());
    for (const auto byte : received_chars) {
        received_bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(byte)));
    }
    assert(received_bytes == object);
    received.close();

    {
        glyph::session::ReceiverSession recovered_complete;
        assert(glyph::session::ReceiverSession::create(receiver_config,
                                                       recovered_complete) ==
               Status::ok);
        assert(recovered_complete.begin_search() == Status::ok);
        assert(recovered_complete.notify_surface_found() == Status::ok);
        assert(recovered_complete.notify_calibrated() == Status::ok);
        assert(recovered_complete.submit_manifest(sender.manifest_bytes()) ==
               Status::ok);
        assert(recovered_complete.state() ==
               glyph::session::ReceiverState::complete);
        assert(recovered_complete.final_path() == resumed.final_path());
    }

    glyph::ResumeJournalIdentity identity;
    identity.transfer_id = sender.manifest().transfer_id;
    identity.object_sha256 = sender.manifest().object_sha256;
    identity.manifest_sha256 = glyph::Sha256::hash(sender.manifest_bytes());
    identity.object_size = sender.manifest().object_size;
    glyph::ResumeJournal journal;
    assert(glyph::ResumeJournal::open(receiver_config.journal_path, identity,
                                      receiver_config.journal_limits, journal) ==
           Status::ok);
    glyph::ResumeJournalState journal_state;
    assert(journal.snapshot(journal_state) == Status::ok);
    assert(!journal_state.verified_blocks.empty());
    journal.close();

    const auto spoof_root = root / "spoof-complete-output";
    std::error_code spoof_root_error;
    assert(std::filesystem::create_directory(spoof_root, spoof_root_error));
    assert(!spoof_root_error);
    {
        std::ofstream spoof_final(spoof_root / "session-output.bin",
                                  std::ios::binary | std::ios::out |
                                      std::ios::trunc);
        assert(spoof_final.is_open());
        spoof_final.write(reinterpret_cast<const char*>(object.data()),
                          static_cast<std::streamsize>(object.size()));
        assert(spoof_final.good());
    }
    const auto spoof_journal_path = root / "spoof-complete.glj";
    glyph::ResumeJournal spoof_journal;
    assert(glyph::ResumeJournal::open(spoof_journal_path, identity,
                                      receiver_config.journal_limits,
                                      spoof_journal) == Status::ok);
    spoof_journal.close();
    append_completion_record(spoof_journal_path);
    glyph::session::ReceiverConfig spoof_config = receiver_config;
    spoof_config.output_root = spoof_root;
    spoof_config.journal_path = spoof_journal_path;
    glyph::session::ReceiverSession spoof_receiver;
    assert(glyph::session::ReceiverSession::create(spoof_config,
                                                   spoof_receiver) == Status::ok);
    assert(spoof_receiver.begin_search() == Status::ok);
    assert(spoof_receiver.notify_surface_found() == Status::ok);
    assert(spoof_receiver.notify_calibrated() == Status::ok);
    assert(spoof_receiver.submit_manifest(sender.manifest_bytes()) ==
           Status::integrity);
    assert(spoof_receiver.state() ==
           glyph::session::ReceiverState::integrity_failure);

    const auto empty_root = root / "empty-output";
    std::error_code empty_root_error;
    assert(std::filesystem::create_directory(empty_root, empty_root_error));
    assert(!empty_root_error);
    glyph::session::SenderConfig empty_sender_config = sender_config;
    empty_sender_config.transfer_id[0] = 0x32U;
    empty_sender_config.display_name = "empty.bin";
    const std::vector<std::byte> empty_object;
    glyph::session::SenderSession empty_sender;
    assert(glyph::session::SenderSession::create(
               empty_object, empty_sender_config, empty_sender) == Status::ok);
    assert(empty_sender.prepare() == Status::ok);
    assert(empty_sender.begin_bootstrap() == Status::ok);
    glyph::session::ReceiverConfig empty_receiver_config = receiver_config;
    empty_receiver_config.output_root = empty_root;
    empty_receiver_config.journal_path = root / "empty.glj";
    glyph::session::ReceiverSession empty_receiver;
    assert(glyph::session::ReceiverSession::create(empty_receiver_config,
                                                   empty_receiver) == Status::ok);
    assert(empty_receiver.begin_search() == Status::ok);
    assert(empty_receiver.notify_surface_found() == Status::ok);
    assert(empty_receiver.notify_calibrated() == Status::ok);
    assert(empty_receiver.submit_manifest(empty_sender.manifest_bytes()) ==
           Status::ok);
    glyph::session::LogicalBlock empty_block;
    bool empty_available = true;
    assert(empty_sender.next_block(empty_block, empty_available) == Status::ok);
    assert(!empty_available);
    assert(empty_sender.state() == glyph::session::SenderState::final_repeat);
    assert(empty_sender.complete_final_repeat() == Status::ok);
    assert(empty_receiver.finalize() == Status::ok);
    assert(std::filesystem::exists(empty_receiver.final_path()));
    assert(std::filesystem::file_size(empty_receiver.final_path()) == 0U);

    const auto source_path = root / "file-source.bin";
    {
        std::ofstream source(source_path, std::ios::binary | std::ios::trunc);
        assert(source.is_open());
        source.write(reinterpret_cast<const char*>(object.data()),
                     static_cast<std::streamsize>(object.size()));
        assert(source.good());
    }
    glyph::session::SenderConfig file_sender_config = sender_config;
    file_sender_config.transfer_id[0] = 0x33U;
    file_sender_config.display_name = "file-source-output.bin";
    glyph::session::SenderSession file_sender;
    assert(glyph::session::SenderSession::open_file(
               source_path, file_sender_config, file_sender) == Status::ok);
    assert(file_sender.prepare() == Status::ok);
    assert(file_sender.begin_bootstrap() == Status::ok);
    std::vector<std::byte> streamed_object;
    for (;;) {
        glyph::session::LogicalBlock block;
        bool available = false;
        assert(file_sender.next_block(block, available) == Status::ok);
        if (!available) {
            break;
        }
        streamed_object.insert(streamed_object.end(), block.bytes.begin(),
                               block.bytes.end());
    }
    assert(streamed_object == object);
    assert(file_sender.state() == glyph::session::SenderState::final_repeat);
    assert(file_sender.complete_final_repeat() == Status::ok);
    assert(file_sender.state() == glyph::session::SenderState::done);

    glyph::session::SenderConfig mutating_sender_config = file_sender_config;
    mutating_sender_config.transfer_id[0] = 0x34U;
    glyph::session::SenderSession mutating_sender;
    assert(glyph::session::SenderSession::open_file(
               source_path, mutating_sender_config, mutating_sender) ==
           Status::ok);
    assert(mutating_sender.prepare() == Status::ok);
    assert(mutating_sender.begin_bootstrap() == Status::ok);
    {
        std::ofstream replacement(source_path,
                                  std::ios::binary | std::ios::trunc);
        assert(replacement.is_open());
        auto mutated_object = object;
        mutated_object.front() ^= std::byte{0x01U};
        replacement.write(reinterpret_cast<const char*>(mutated_object.data()),
                          static_cast<std::streamsize>(mutated_object.size()));
        assert(replacement.good());
    }
    for (;;) {
        glyph::session::LogicalBlock block;
        bool available = false;
        const auto status = mutating_sender.next_block(block, available);
        if (status != Status::ok) {
            assert(status == Status::integrity);
            assert(mutating_sender.state() ==
                   glyph::session::SenderState::integrity_failure);
            break;
        }
        assert(available);
    }
    file_sender.cancel();
    mutating_sender.cancel();

    const auto limited_root = root / "limited-output";
    std::error_code limited_root_error;
    assert(std::filesystem::create_directory(limited_root, limited_root_error));
    assert(!limited_root_error);
    glyph::session::ReceiverConfig limited_config = receiver_config;
    limited_config.output_root = limited_root;
    limited_config.journal_path = root / "limited.glj";
    limited_config.object_limits.max_object_size = object.size() - 1U;
    glyph::session::ReceiverSession limited;
    assert(glyph::session::ReceiverSession::create(limited_config, limited) ==
           Status::ok);
    assert(limited.begin_search() == Status::ok);
    assert(limited.notify_surface_found() == Status::ok);
    assert(limited.notify_calibrated() == Status::ok);
    assert(limited.submit_manifest(sender.manifest_bytes()) ==
           Status::resource_limit);
    assert(limited.state() == glyph::session::ReceiverState::resource_limit);

    const auto invalid_journal_root = root / "invalid-journal-output";
    std::error_code invalid_root_error;
    assert(std::filesystem::create_directory(invalid_journal_root,
                                              invalid_root_error));
    assert(!invalid_root_error);
    const auto invalid_journal_path = root / "invalid-range.glj";
    glyph::ResumeJournal invalid_journal;
    assert(glyph::ResumeJournal::open(invalid_journal_path, identity,
                                      receiver_config.journal_limits,
                                      invalid_journal) == Status::ok);
    assert(invalid_journal.append_verified_block(1U) == Status::ok);
    invalid_journal.close();
    glyph::session::ReceiverConfig invalid_config = receiver_config;
    invalid_config.output_root = invalid_journal_root;
    invalid_config.journal_path = invalid_journal_path;
    glyph::session::ReceiverSession invalid_resume;
    assert(glyph::session::ReceiverSession::create(invalid_config,
                                                   invalid_resume) == Status::ok);
    assert(invalid_resume.begin_search() == Status::ok);
    assert(invalid_resume.notify_surface_found() == Status::ok);
    assert(invalid_resume.notify_calibrated() == Status::ok);
    assert(invalid_resume.submit_manifest(sender.manifest_bytes()) ==
           Status::integrity);
    assert(invalid_resume.state() ==
           glyph::session::ReceiverState::integrity_failure);

    glyph::session::ReceiverSession out_of_order;
    const auto out_of_order_root = root / "out-of-order-output";
    std::error_code create_output_error;
    assert(std::filesystem::create_directory(out_of_order_root,
                                              create_output_error));
    assert(!create_output_error);
    receiver_config.journal_path = root / "out-of-order.glj";
    receiver_config.output_root = out_of_order_root;
    assert(glyph::session::ReceiverSession::create(receiver_config, out_of_order) ==
           Status::ok);
    assert(out_of_order.begin_search() == Status::ok);
    assert(out_of_order.notify_surface_found() == Status::ok);
    assert(out_of_order.notify_calibrated() == Status::ok);
    assert(out_of_order.submit_manifest(sender.manifest_bytes()) == Status::ok);
    assert(out_of_order.submit_block(1U, object) == Status::invalid_argument);
    out_of_order.cancel();
    assert(!std::filesystem::exists(receiver_config.journal_path));

    glyph::session::ReceiverConfig unsupported_config = receiver_config;
    unsupported_config.journal_path = root / "unsupported.glj";
    unsupported_config.supported_fec_profile = "other";
    glyph::session::ReceiverSession unsupported;
    assert(glyph::session::ReceiverSession::create(unsupported_config,
                                                   unsupported) == Status::ok);
    assert(unsupported.begin_search() == Status::ok);
    assert(unsupported.notify_surface_found() == Status::ok);
    assert(unsupported.notify_calibrated() == Status::ok);
    assert(unsupported.submit_manifest(sender.manifest_bytes()) ==
           Status::unsupported);
    assert(unsupported.state() ==
           glyph::session::ReceiverState::unsupported_profile);

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    assert(!cleanup_error);
    return 0;
}
