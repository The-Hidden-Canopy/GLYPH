#pragma once

#include "glyph/crypto/sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace glyph {

struct Manifest {
    std::string protocol = "glyph/1";
    std::array<std::uint8_t, 16> transfer_id{};
    Digest256 object_sha256{};
    std::uint64_t object_size = 0;
    std::string display_name;
    std::optional<std::string> media_type;
    std::uint64_t block_size = 0;
    std::uint64_t shard_size = 0;
    std::string fec_profile;
    std::optional<std::string> created_at;
};

struct ManifestLimits {
    std::uint64_t max_object_size = 1ULL << 40U;
    std::uint64_t max_block_size = 16ULL << 20U;
    std::uint64_t max_shard_size = 16ULL << 20U;
    std::size_t max_manifest_bytes = 1U << 20U;
    std::size_t max_display_name_bytes = 255U;
    std::size_t max_media_type_bytes = 255U;
    std::size_t max_fec_profile_bytes = 64U;
    std::size_t max_created_at_bytes = 64U;
};

struct ManifestValidation {
    Status status = Status::ok;
    std::string reason;
};

struct EncodedManifest {
    Status status = Status::ok;
    std::vector<std::byte> bytes;
    std::string reason;
};

struct DecodedManifest {
    Status status = Status::ok;
    Manifest manifest{};
    std::string reason;
};

[[nodiscard]] ManifestValidation validate_manifest(
    const Manifest& manifest,
    const ManifestLimits& limits = {});

[[nodiscard]] EncodedManifest encode_manifest(
    const Manifest& manifest,
    const ManifestLimits& limits = {});

[[nodiscard]] DecodedManifest decode_manifest(
    std::span<const std::byte> encoded,
    const ManifestLimits& limits = {});

}  // namespace glyph
