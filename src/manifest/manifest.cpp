#include "glyph/manifest/manifest.hpp"

#include "glyph/core/version.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

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

class CborReader final {
public:
    explicit CborReader(const std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] Status read_head(std::uint8_t& major,
                                   std::uint64_t& value) {
        if (offset_ >= bytes_.size()) {
            return Status::protocol;
        }
        const auto initial = std::to_integer<std::uint8_t>(bytes_[offset_++]);
        major = static_cast<std::uint8_t>(initial >> 5U);
        const auto additional = static_cast<std::uint8_t>(initial & 0x1fU);
        if (additional < 24U) {
            value = additional;
            return Status::ok;
        }
        if (additional == 31U) {
            return Status::protocol;
        }

        unsigned int byte_count = 0U;
        if (additional == 24U) {
            byte_count = 1U;
        } else if (additional == 25U) {
            byte_count = 2U;
        } else if (additional == 26U) {
            byte_count = 4U;
        } else if (additional == 27U) {
            byte_count = 8U;
        } else {
            return Status::protocol;
        }
        if (bytes_.size() - offset_ < byte_count) {
            return Status::protocol;
        }

        value = 0U;
        for (unsigned int index = 0U; index < byte_count; ++index) {
            value = (value << 8U) |
                    std::to_integer<std::uint8_t>(bytes_[offset_++]);
        }
        if ((additional == 24U && value < 24U) ||
            (additional == 25U && value <= 0xffU) ||
            (additional == 26U && value <= 0xffffU) ||
            (additional == 27U && value <= 0xffffffffULL)) {
            return Status::protocol;
        }
        return Status::ok;
    }

    [[nodiscard]] Status read_uint(std::uint64_t& value) {
        std::uint8_t major = 0U;
        const auto status = read_head(major, value);
        if (status != Status::ok || major != 0U) {
            return Status::protocol;
        }
        return Status::ok;
    }

    [[nodiscard]] Status read_text(std::string& value,
                                   const std::size_t max_bytes) {
        std::uint8_t major = 0U;
        std::uint64_t length = 0U;
        if (read_head(major, length) != Status::ok || major != 3U) {
            return Status::protocol;
        }
        if (length > max_bytes || length > bytes_.size() - offset_) {
            return length > max_bytes ? Status::resource_limit
                                      : Status::protocol;
        }
        value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_),
                     static_cast<std::size_t>(length));
        offset_ += static_cast<std::size_t>(length);
        return Status::ok;
    }

    [[nodiscard]] Status read_bytes(std::vector<std::byte>& value,
                                    const std::size_t expected_bytes) {
        std::uint8_t major = 0U;
        std::uint64_t length = 0U;
        if (read_head(major, length) != Status::ok || major != 2U) {
            return Status::protocol;
        }
        if (length != expected_bytes || length > bytes_.size() - offset_) {
            return Status::protocol;
        }
        value.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                     bytes_.begin() +
                         static_cast<std::ptrdiff_t>(offset_ + length));
        offset_ += static_cast<std::size_t>(length);
        return Status::ok;
    }

    [[nodiscard]] Status read_null() {
        if (offset_ >= bytes_.size() ||
            std::to_integer<std::uint8_t>(bytes_[offset_]) != 0xf6U) {
            return Status::protocol;
        }
        ++offset_;
        return Status::ok;
    }

    [[nodiscard]] Status read_empty_map() {
        std::uint8_t major = 0U;
        std::uint64_t count = 0U;
        if (read_head(major, count) != Status::ok || major != 5U ||
            count != 0U) {
            return Status::protocol;
        }
        return Status::ok;
    }

    [[nodiscard]] bool at_end() const noexcept {
        return offset_ == bytes_.size();
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_ = 0U;
};

DecodedManifest decode_failure(const Status status, std::string reason) {
    return DecodedManifest{status, {}, std::move(reason)};
}

Status read_key(CborReader& reader, const std::string_view expected) {
    std::string actual;
    const auto status = reader.read_text(actual, 32U);
    if (status != Status::ok) {
        return status;
    }
    return actual == expected ? Status::ok : Status::protocol;
}

Status read_optional_text(CborReader& reader,
                          std::optional<std::string>& value,
                          const std::size_t max_bytes) {
    if (reader.read_null() == Status::ok) {
        value.reset();
        return Status::ok;
    }
    std::string text;
    const auto status = reader.read_text(text, max_bytes);
    if (status == Status::ok) {
        value = std::move(text);
    }
    return status;
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

    append_key(output, "shard_size");
    append_uint(output, manifest.shard_size);

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

DecodedManifest decode_manifest(const std::span<const std::byte> encoded,
                                const ManifestLimits& limits) {
    if (encoded.size() > limits.max_manifest_bytes) {
        return decode_failure(Status::resource_limit,
                              "encoded manifest exceeds configured limit");
    }

    CborReader reader(encoded);
    std::uint8_t major = 0U;
    std::uint64_t field_count = 0U;
    if (reader.read_head(major, field_count) != Status::ok || major != 5U ||
        field_count != kManifestFieldCount) {
        return decode_failure(Status::protocol, "manifest map shape is invalid");
    }

    Manifest manifest;
    std::string text;
    std::uint64_t number = 0U;
    std::vector<std::byte> bytes;

    if (read_key(reader, "protocol") != Status::ok ||
        reader.read_text(text, 16U) != Status::ok) {
        return decode_failure(Status::protocol, "invalid protocol field");
    }
    manifest.protocol = std::move(text);

    if (read_key(reader, "block_size") != Status::ok ||
        reader.read_uint(number) != Status::ok) {
        return decode_failure(Status::protocol, "invalid block_size field");
    }
    manifest.block_size = number;

    if (read_key(reader, "created_at") != Status::ok ||
        read_optional_text(reader, manifest.created_at,
                           limits.max_created_at_bytes) != Status::ok) {
        return decode_failure(Status::protocol, "invalid created_at field");
    }

    if (read_key(reader, "encryption") != Status::ok ||
        reader.read_null() != Status::ok) {
        return decode_failure(Status::unsupported,
                              "secure encryption field is not implemented");
    }

    if (read_key(reader, "extensions") != Status::ok ||
        reader.read_empty_map() != Status::ok) {
        return decode_failure(Status::unsupported,
                              "manifest extensions are not implemented");
    }

    if (read_key(reader, "media_type") != Status::ok ||
        read_optional_text(reader, manifest.media_type,
                           limits.max_media_type_bytes) != Status::ok) {
        return decode_failure(Status::protocol, "invalid media_type field");
    }

    if (read_key(reader, "shard_size") != Status::ok ||
        reader.read_uint(number) != Status::ok) {
        return decode_failure(Status::protocol, "invalid shard_size field");
    }
    manifest.shard_size = number;

    if (read_key(reader, "fec_profile") != Status::ok ||
        reader.read_text(manifest.fec_profile,
                         limits.max_fec_profile_bytes) != Status::ok) {
        return decode_failure(Status::protocol, "invalid fec_profile field");
    }

    if (read_key(reader, "object_size") != Status::ok ||
        reader.read_uint(manifest.object_size) != Status::ok) {
        return decode_failure(Status::protocol, "invalid object_size field");
    }

    if (read_key(reader, "transfer_id") != Status::ok ||
        reader.read_bytes(bytes, manifest.transfer_id.size()) != Status::ok) {
        return decode_failure(Status::protocol, "invalid transfer_id field");
    }
    for (std::size_t index = 0U; index < manifest.transfer_id.size(); ++index) {
        manifest.transfer_id[index] = std::to_integer<std::uint8_t>(bytes[index]);
    }

    if (read_key(reader, "display_name") != Status::ok ||
        reader.read_text(manifest.display_name,
                         limits.max_display_name_bytes) != Status::ok) {
        return decode_failure(Status::protocol, "invalid display_name field");
    }

    if (read_key(reader, "object_sha256") != Status::ok ||
        reader.read_bytes(bytes, manifest.object_sha256.bytes.size()) !=
            Status::ok) {
        return decode_failure(Status::protocol, "invalid object_sha256 field");
    }
    for (std::size_t index = 0U; index < manifest.object_sha256.bytes.size();
         ++index) {
        manifest.object_sha256.bytes[index] =
            std::to_integer<std::uint8_t>(bytes[index]);
    }

    if (!reader.at_end()) {
        return decode_failure(Status::protocol,
                              "trailing bytes after manifest");
    }

    const auto validation = validate_manifest(manifest, limits);
    if (validation.status != Status::ok) {
        return decode_failure(validation.status, validation.reason);
    }
    return DecodedManifest{Status::ok, std::move(manifest), {}};
}

}  // namespace glyph
