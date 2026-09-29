#include "glyph/transport/resume_journal.hpp"

#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string bytes_hex(const std::vector<std::byte>& bytes) {
    constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(bytes.size() * 2U);
    for (const auto byte : bytes) {
        const auto value = std::to_integer<unsigned int>(byte);
        output.push_back(hex[(value >> 4U) & 0x0fU]);
        output.push_back(hex[value & 0x0fU]);
    }
    return output;
}

std::filesystem::path make_root() {
    const auto base = std::filesystem::temp_directory_path();
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned int attempt = 0U; attempt < 16U; ++attempt) {
        const auto candidate =
            base / ("glyph-journal-" + std::to_string(ticks) + "-" +
                    std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error) && !error) {
            return candidate;
        }
    }
    return {};
}

std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    assert(stream.is_open());
    const auto size = std::filesystem::file_size(path);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    assert(stream.good() || stream.eof());
    return bytes;
}

std::string read_vector_hex() {
    const auto source_root = std::filesystem::path(__FILE__)
                                 .parent_path()
                                 .parent_path()
                                 .parent_path();
    std::ifstream stream(source_root / "vectors" /
                             "glj2-completion-record-v1.hex");
    assert(stream.is_open());
    std::string value;
    char character = 0;
    while (stream.get(character)) {
        if (!std::isspace(static_cast<unsigned char>(character))) {
            value.push_back(character);
        }
    }
    assert(stream.eof());
    return value;
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

glyph::ResumeJournalIdentity identity() {
    glyph::ResumeJournalIdentity value;
    value.transfer_id = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U,
                         9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U};
    value.object_size = 123456U;
    for (std::size_t index = 0U; index < value.object_sha256.bytes.size();
         ++index) {
        value.object_sha256.bytes[index] = static_cast<std::uint8_t>(index);
        value.manifest_sha256.bytes[index] =
            static_cast<std::uint8_t>(0xffU - index);
    }
    return value;
}

}  // namespace

int main() {
    const auto root = make_root();
    assert(!root.empty());
    const auto path = root / "resume.glj";
    const auto expected_identity = identity();

    const auto vector_path = root / "completion-vector.glj";
    glyph::ResumeJournal vector_journal;
    assert(glyph::ResumeJournal::open(
               vector_path, expected_identity, glyph::ResumeJournalLimits{},
               vector_journal) == glyph::Status::ok);
    vector_journal.close();
    append_completion_record(vector_path);
    assert(glyph::ResumeJournal::open(
               vector_path, expected_identity, glyph::ResumeJournalLimits{},
               vector_journal) == glyph::Status::ok);
    glyph::ResumeJournalState vector_state;
    assert(vector_journal.snapshot(vector_state) == glyph::Status::ok);
    assert(vector_state.completed);
    vector_journal.close();
    const auto emitted_vector = bytes_hex(read_bytes(vector_path));
    assert(emitted_vector ==
           "474c4a32000200640102030405060708090a0b0c0d0e0f10000102030405060708"
           "090a0b0c0d0e0f101112131415161718191a1b1c1d1e1ffffefdfcfbfaf9f8f7f6"
           "f5f4f3f2f1f0efeeedecebeae9e8e7e6e5e4e3e2e1e0000000000001e240e13f614f"
           "0000000103412da0a5");
    assert(emitted_vector == read_vector_hex());

    glyph::ResumeJournal journal;
    assert(glyph::ResumeJournal::open(path, expected_identity,
                                      glyph::ResumeJournalLimits{}, journal) ==
           glyph::Status::ok);
    glyph::ResumeJournal concurrent_journal;
    assert(glyph::ResumeJournal::open(
               path, expected_identity, glyph::ResumeJournalLimits{},
               concurrent_journal) == glyph::Status::io);
    glyph::ResumeJournalState state;
    assert(journal.snapshot(state) == glyph::Status::ok);
    assert(state.identity == expected_identity);
    assert(state.shard_receipts.empty());
    assert(state.verified_blocks.empty());

    assert(journal.append_shard(2U, 7U) == glyph::Status::ok);
    assert(journal.append_shard(2U, 7U) == glyph::Status::ok);
    assert(journal.append_shard(2U, 8U) == glyph::Status::ok);
    assert(journal.append_verified_block(8U) == glyph::Status::ok);
    assert(journal.append_verified_block(10U) == glyph::Status::ok);
    assert(journal.append_verified_block(9U) == glyph::Status::ok);
    assert(journal.append_verified_block(8U) == glyph::Status::ok);
    assert(journal.snapshot(state) == glyph::Status::ok);
    assert(state.shard_receipts.size() == 2U);
    assert(state.verified_blocks.size() == 1U);
    assert((state.verified_blocks[0U] ==
            glyph::ResumeBlockRange{8U, 3U}));

    journal.close();
    assert(glyph::ResumeJournal::open(path, expected_identity,
                                      glyph::ResumeJournalLimits{}, journal) ==
           glyph::Status::ok);
    assert(journal.snapshot(state) == glyph::Status::ok);
    assert(state.shard_receipts.size() == 2U);
    assert(state.verified_blocks.size() == 1U);
    assert(state.verified_blocks[0U].first_block_id == 8U);
    assert(state.verified_blocks[0U].block_count == 3U);
    assert(!state.completed);
    journal.close();
    append_completion_record(path);
    assert(glyph::ResumeJournal::open(path, expected_identity,
                                      glyph::ResumeJournalLimits{}, journal) ==
           glyph::Status::ok);
    assert(journal.append_verified_block(11U) == glyph::Status::invalid_argument);
    assert(journal.snapshot(state) == glyph::Status::ok);
    assert(state.completed);

    // The journal is single-writer. Release the lock before checking an
    // identity mismatch so the result exercises the header validation rather
    // than the concurrent-writer guard.
    journal.close();

    const auto version_path = root / "old-version.glj";
    glyph::ResumeJournal version_journal;
    assert(glyph::ResumeJournal::open(version_path, expected_identity,
                                      glyph::ResumeJournalLimits{},
                                      version_journal) == glyph::Status::ok);
    version_journal.close();
    {
        std::fstream mutate(version_path, std::ios::binary | std::ios::in |
                                            std::ios::out);
        assert(mutate.is_open());
        mutate.seekp(3, std::ios::beg);
        mutate.put('1');
        mutate.flush();
        assert(mutate.good());
    }
    glyph::ResumeJournal rejected_version;
    assert(glyph::ResumeJournal::open(
               version_path, expected_identity, glyph::ResumeJournalLimits{},
               rejected_version) == glyph::Status::protocol);

    const auto corrupt_path = root / "corrupt.glj";
    glyph::ResumeJournal corrupt_journal;
    assert(glyph::ResumeJournal::open(corrupt_path, expected_identity,
                                      glyph::ResumeJournalLimits{},
                                      corrupt_journal) == glyph::Status::ok);
    assert(corrupt_journal.append_shard(3U, 1U) == glyph::Status::ok);
    corrupt_journal.close();
    {
        std::fstream mutate(corrupt_path, std::ios::binary | std::ios::in |
                                           std::ios::out);
        assert(mutate.is_open());
        mutate.seekg(-1, std::ios::end);
        char value = 0;
        mutate.get(value);
        assert(mutate.good());
        mutate.clear();
        mutate.seekp(-1, std::ios::end);
        mutate.put(static_cast<char>(value ^ static_cast<char>(0x01)));
        mutate.flush();
        assert(mutate.good());
    }
    glyph::ResumeJournal rejected_corruption;
    assert(glyph::ResumeJournal::open(
               corrupt_path, expected_identity, glyph::ResumeJournalLimits{},
               rejected_corruption) == glyph::Status::integrity);

    auto wrong_identity = expected_identity;
    wrong_identity.object_size += 1U;
    glyph::ResumeJournal wrong_journal;
    assert(glyph::ResumeJournal::open(path, wrong_identity,
                                      glyph::ResumeJournalLimits{},
                                      wrong_journal) == glyph::Status::integrity);

    journal.close();
    {
        std::ofstream append(path, std::ios::binary | std::ios::out |
                                      std::ios::app);
        assert(append.is_open());
        append.put(static_cast<char>(0x01));
        append.put(static_cast<char>(0x02));
        assert(append.good());
    }
    const auto size_with_partial_tail = std::filesystem::file_size(path);
    assert(glyph::ResumeJournal::open(path, expected_identity,
                                      glyph::ResumeJournalLimits{}, journal) ==
           glyph::Status::ok);
    assert(journal.journal_bytes() < size_with_partial_tail);
    assert(journal.snapshot(state) == glyph::Status::ok);
    assert(state.shard_receipts.size() == 2U);
    assert(state.completed);

    glyph::ResumeJournalLimits receipt_limit;
    receipt_limit.max_shard_receipts = 1U;
    const auto limited_receipt_path = root / "receipts.glj";
    glyph::ResumeJournal limited_receipts;
    assert(glyph::ResumeJournal::open(limited_receipt_path, expected_identity,
                                      receipt_limit, limited_receipts) ==
           glyph::Status::ok);
    assert(limited_receipts.append_shard(1U, 1U) == glyph::Status::ok);
    assert(limited_receipts.append_shard(1U, 2U) ==
           glyph::Status::resource_limit);

    glyph::ResumeJournalLimits block_limit;
    block_limit.max_verified_blocks = 1U;
    const auto limited_block_path = root / "blocks.glj";
    glyph::ResumeJournal limited_blocks;
    assert(glyph::ResumeJournal::open(limited_block_path, expected_identity,
                                      block_limit, limited_blocks) ==
           glyph::Status::ok);
    assert(limited_blocks.append_verified_block(1U) == glyph::Status::ok);
    assert(limited_blocks.append_verified_block(3U) ==
           glyph::Status::resource_limit);

    glyph::ResumeJournalLimits tiny;
    tiny.max_journal_bytes = 100U;
    const auto tiny_path = root / "tiny.glj";
    glyph::ResumeJournal tiny_journal;
    assert(glyph::ResumeJournal::open(tiny_path, expected_identity, tiny,
                                      tiny_journal) == glyph::Status::ok);
    assert(tiny_journal.append_shard(1U, 1U) ==
           glyph::Status::resource_limit);

    journal.close();
    limited_receipts.close();
    limited_blocks.close();
    tiny_journal.close();
    wrong_journal.close();
    rejected_corruption.close();
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    assert(!cleanup_error);
    return 0;
}
