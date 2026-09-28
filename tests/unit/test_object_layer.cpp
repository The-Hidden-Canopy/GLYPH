#include "glyph/manifest/manifest.hpp"
#include "glyph/transport/object_io.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> bytes_from_string(const std::string& value) {
    const auto* data = reinterpret_cast<const std::byte*>(value.data());
    return {data, data + value.size()};
}

std::string bytes_hex(const std::vector<std::byte>& bytes) {
    constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(bytes.size() * 2U);
    for (const auto byte : bytes) {
        const auto value = static_cast<unsigned int>(byte);
        output.push_back(hex[(value >> 4U) & 0x0fU]);
        output.push_back(hex[value & 0x0fU]);
    }
    return output;
}

glyph::Manifest sample_manifest(const glyph::Digest256& digest,
                                const std::uint64_t object_size) {
    glyph::Manifest manifest;
    manifest.transfer_id = {0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U,
                            0x07U, 0x08U, 0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU,
                            0x0eU, 0x0fU};
    manifest.object_sha256 = digest;
    manifest.object_size = object_size;
    manifest.display_name = "sample.bin";
    manifest.media_type = "application/octet-stream";
    manifest.block_size = 1024U;
    manifest.shard_size = 256U;
    manifest.fec_profile = "RS32+8";
    return manifest;
}

std::filesystem::path make_test_root() {
    const auto base = std::filesystem::temp_directory_path();
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned int attempt = 0U; attempt < 16U; ++attempt) {
        const auto candidate =
            base / ("glyph-wp01-" + std::to_string(ticks) + "-" +
                    std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error) && !error) {
            return candidate;
        }
    }
    return {};
}

void write_bytes(const std::filesystem::path& path,
                 const std::vector<std::byte>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::out | std::ios::trunc);
    assert(stream.is_open());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    assert(stream.good());
}

}  // namespace

int main() {
    const auto object = bytes_from_string("GLYPH object layer\n");
    const auto object_span = std::span<const std::byte>(object.data(), object.size());
    const auto digest = glyph::Sha256::hash(object_span);

    auto manifest = sample_manifest(digest, object.size());
    const auto encoded = glyph::encode_manifest(manifest);
    assert(encoded.status == glyph::Status::ok);
    const auto encoded_again = glyph::encode_manifest(manifest);
    assert(encoded_again.status == glyph::Status::ok);
    assert(encoded.bytes == encoded_again.bytes);
    assert(!encoded.bytes.empty());
    assert(encoded.bytes.front() == static_cast<std::byte>(0xacU));
    assert(bytes_hex(encoded.bytes) ==
           "ac6870726f746f636f6c67676c7970682f316a626c6f636b5f73697a65190400"
           "6a637265617465645f6174f66a656e6372797074696f6ef66a657874656e73696f"
           "6e73a06a6d656469615f7479706578186170706c69636174696f6e2f6f637465742d"
           "73747265616d6b6665635f70726f66696c6566525333322b386b6f626a6563745f"
           "73697a65136b7472616e736665725f696450000102030405060708090a0b0c0d0e0f"
           "6c646973706c61795f6e616d656a73616d706c652e62696e6d6f626a6563745f7368"
           "613235365820b06db03b26e8d72ba8fa7528e758d4c26e71f76e23695328dda3b593"
           "bf0727f8");

    auto invalid = manifest;
    invalid.transfer_id.fill(0U);
    assert(glyph::validate_manifest(invalid).status == glyph::Status::invalid_argument);

    invalid = manifest;
    invalid.display_name = "bad\nname";
    assert(glyph::validate_manifest(invalid).status == glyph::Status::invalid_argument);

    glyph::ManifestLimits tiny_limits;
    tiny_limits.max_object_size = 4U;
    assert(glyph::validate_manifest(manifest, tiny_limits).status ==
           glyph::Status::resource_limit);

    const auto root = make_test_root();
    assert(!root.empty());
    const auto source = root / "source.bin";
    write_bytes(source, object);

    const auto hashed = glyph::hash_object_file(source);
    assert(hashed.status == glyph::Status::ok);
    assert(hashed.object_size == object.size());
    assert(hashed.sha256 == digest);
    assert(glyph::verify_object_file(source, object.size(), digest) ==
           glyph::Status::ok);

    auto wrong_digest = digest;
    wrong_digest.bytes[0] ^= 0xffU;
    assert(glyph::verify_object_file(source, object.size(), wrong_digest) ==
           glyph::Status::integrity);

    glyph::AtomicObjectWriter writer;
    assert(glyph::AtomicObjectWriter::open(root, "../received.bin",
                                           glyph::ObjectLimits{}, writer) ==
           glyph::Status::ok);
    assert(writer.final_path().parent_path() == root);
    assert(writer.final_path().filename() == ".._received.bin");
    assert(writer.write(object_span.first(4U)) == glyph::Status::ok);
    assert(writer.write(object_span.subspan(4U)) == glyph::Status::ok);
    assert(writer.finalize(object.size(), digest) == glyph::Status::ok);
    assert(std::filesystem::exists(root / ".._received.bin"));

    glyph::AtomicObjectWriter failed_writer;
    assert(glyph::AtomicObjectWriter::open(root, "failed.bin",
                                           glyph::ObjectLimits{}, failed_writer) ==
           glyph::Status::ok);
    assert(failed_writer.write(object_span) == glyph::Status::ok);
    assert(failed_writer.finalize(object.size(), wrong_digest) ==
           glyph::Status::integrity);
    assert(!std::filesystem::exists(root / "failed.bin"));

    glyph::ObjectLimits four_bytes;
    four_bytes.max_object_size = 4U;
    glyph::AtomicObjectWriter limited_writer;
    assert(glyph::AtomicObjectWriter::open(root, "limited.bin", four_bytes,
                                           limited_writer) == glyph::Status::ok);
    assert(limited_writer.write(object_span) == glyph::Status::resource_limit);
    limited_writer.abort();
    assert(!std::filesystem::exists(root / "limited.bin"));

    glyph::AtomicObjectWriter duplicate_writer;
    assert(glyph::AtomicObjectWriter::open(root, "../received.bin",
                                           glyph::ObjectLimits{}, duplicate_writer) ==
           glyph::Status::io);

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    assert(!cleanup_error);
    return 0;
}
