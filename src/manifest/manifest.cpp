#include "glyph/manifest/manifest.hpp"

#include "glyph/core/version.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <span>

namespace glyph {
namespace {

constexpr std::size_t kManifestFieldCount = 12U;

bool contains_control_byte(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte < 0x20U || byte == 0x7fU;
    });
}

bool is_all_zero(const std::array<std::uint8_t, 16>& value) {
    return std::all_of(value.begin(), value.end(), [](const auto byte) {
        return byte == 0U;
    });
}

ManifestValidation failure(const Status status, std::string reason) {
    return ManifestValidation{status, std::move(reason)};
}

void append_byte(std::vector<std::byte>& output, const std::uint8_t value) {
    output.push_back(static_cast<std::byte>(value));
}

void append_big_endian(std::vector<std::byte>& output,
                       const std::uint64_t value,
                       const unsigned int byte_count) {
    for (unsigned int index = 0U; index < byte_count; ++index) {
        append_byte(output,
                    static_cast<std::uint8_t>(value >>
                                              ((byte_count - index - 1U) * 8U)));
    }
}

void append_type_and_length(std::vector<std::byte>& output,
                            const std::uint8_t major_type,
                            const std::uint64_t length) {
    if (length < 24U) {
        append_byte(output,
                    static_cast<std::uint8_t>((major_type << 5U) | length));
    } else if (length <= 0xffU) {
        append_byte(output, static_cast<std::uint8_t>((major_type << 5U) | 24U));
        append_big_endian(output, length, 1U);
    } else if (length <= 0xffffU) {
        append_byte(output, static_cast<std::uint8_t>((major_type << 5U) | 25U));
        append_big_endian(output, length, 2U);
    } else if (length <= 0xffffffffULL) {
        append_byte(output, static_cast<std::uint8_t>((major_type << 5U) | 26U));
        append_big_endian(output, length, 4U);
    } else {
        append_byte(output, static_cast<std::uint8_t>((major_type << 5U) | 27U));
        append_big_endian(output, length, 8U);
    }
}

void append_uint(std::vector<std::byte>& output, const std::uint64_t value) {
    append_type_and_length(output, 0U, value);
}

void append_text(std::vector<std::byte>& output, const std::string& value) {
    append_type_and_length(output, 3U, value.size());
    const auto bytes = std::as_bytes(std::span<const char>(value.data(), value.size()));
    output.insert(output.end(), bytes.begin(), bytes.end());
}

void append_key(std::vector<std::byte>& output, const char* key) {
    append_text(output, key);
}

void append_bytes(std::vector<std::byte>& output,
                  const std::uint8_t* data,
                  const std::size_t size) {
    append_type_and_length(output, 2U, size);
    for (std::size_t index = 0U; index < size; ++index) {
        append_byte(output, data[index]);
    }
}

void append_null(std::vector<std::byte>& output) {
    append_byte(output, 0xf6U);
}

}  // namespace

ManifestValidation validate_manifest(const Manifest& manifest,
                                     const ManifestLimits& limits) {
    if (manifest.protocol != protocol_name) {
        return failure(Status::protocol, "unsupported protocol identifier");
    }
    if (is_all_zero(manifest.transfer_id)) {
        return failure(Status::invalid_argument, "transfer_id must not be zero");
    }
    if (manifest.object_size > limits.max_object_size) {
        return failure(Status::resource_limit, "object_size exceeds configured limit");
    }
    if (manifest.display_name.empty() ||
        manifest.display_name.size() > limits.max_display_name_bytes ||
        contains_control_byte(manifest.display_name)) {
        return failure(Status::invalid_argument, "invalid display_name");
    }
    if (manifest.media_type.has_value() &&
        (manifest.media_type->size() > limits.max_media_type_bytes ||
         contains_control_byte(*manifest.media_type))) {
        return failure(Status::invalid_argument, "invalid media_type");
    }
    if (manifest.block_size == 0U ||
        manifest.block_size > limits.max_block_size) {
        return failure(Status::invalid_argument, "invalid block_size");
    }
    if (manifest.shard_size == 0U ||
        manifest.shard_size > limits.max_shard_size) {
        return failure(Status::invalid_argument, "invalid shard_size");
    }
    if (manifest.fec_profile.empty() ||
        manifest.fec_profile.size() > limits.max_fec_profile_bytes ||
        contains_control_byte(manifest.fec_profile)) {
        return failure(Status::invalid_argument, "invalid fec_profile");
    }
    if (manifest.created_at.has_value() &&
        (manifest.created_at->size() > limits.max_created_at_bytes ||
         contains_control_byte(*manifest.created_at))) {
        return failure(Status::invalid_argument, "invalid created_at");
    }

    return {};
}

EncodedManifest encode_manifest(const Manifest& manifest,
                                const ManifestLimits& limits) {
    const auto validation = validate_manifest(manifest, limits);
    if (validation.status != Status::ok) {
        return EncodedManifest{validation.status, {}, validation.reason};
    }

    std::vector<std::byte> output;
    output.reserve(512U);
    append_type_and_length(output, 5U, kManifestFieldCount);

    // Canonical CBOR map order: text keys are sorted by encoded length, then
    // lexicographically. Encryption is null and extensions are empty until
    // their profile-specific representations are standardized.
    append_key(output, "protocol");
    append_text(output, manifest.protocol);

    append_key(output, "block_size");
    append_uint(output, manifest.block_size);

    append_key(output, "created_at");
    if (manifest.created_at.has_value()) {
        append_text(output, *manifest.created_at);
    } else {
        append_null(output);
    }

    append_key(output, "encryption");
    append_null(output);

    append_key(output, "extensions");
    append_type_and_length(output, 5U, 0U);

    append_key(output, "media_type");
    if (manifest.media_type.has_value()) {
        append_text(output, *manifest.media_type);
    } else {
        append_null(output);
    }

    append_key(output, "fec_profile");
    append_text(output, manifest.fec_profile);

    append_key(output, "object_size");
    append_uint(output, manifest.object_size);

    append_key(output, "transfer_id");
    append_bytes(output, manifest.transfer_id.data(), manifest.transfer_id.size());

    append_key(output, "display_name");
    append_text(output, manifest.display_name);

    append_key(output, "object_sha256");
    append_bytes(output, manifest.object_sha256.bytes.data(),
                 manifest.object_sha256.bytes.size());

    if (output.size() > limits.max_manifest_bytes) {
        return EncodedManifest{Status::resource_limit, {},
                               "encoded manifest exceeds configured limit"};
    }

    return EncodedManifest{Status::ok, std::move(output), {}};
}

}  // namespace glyph

