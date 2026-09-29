#include "glyph/image/png.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace glyph::image {
namespace {

constexpr std::array<std::uint8_t, 8> kPngSignature{
    0x89U, 0x50U, 0x4eU, 0x47U, 0x0dU, 0x0aU, 0x1aU, 0x0aU};
constexpr std::size_t kRgbaBytesPerPixel = 4U;

constexpr std::array<std::uint32_t, 29> kLengthBase{
    3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 13U,
    15U, 17U, 19U, 23U, 27U, 31U, 35U, 43U, 51U, 59U,
    67U, 83U, 99U, 115U, 131U, 163U, 195U, 227U, 258U};
constexpr std::array<std::uint8_t, 29> kLengthExtra{
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 1U,
    1U, 1U, 2U, 2U, 2U, 2U, 3U, 3U, 3U, 3U,
    4U, 4U, 4U, 4U, 5U, 5U, 5U, 5U, 0U};
constexpr std::array<std::uint32_t, 30> kDistanceBase{
    1U, 2U, 3U, 4U, 5U, 7U, 9U, 13U, 17U, 25U,
    33U, 49U, 65U, 97U, 129U, 193U, 257U, 385U, 513U, 769U,
    1025U, 1537U, 2049U, 3073U, 4097U, 6145U, 8193U, 12289U,
    16385U, 24577U};
constexpr std::array<std::uint8_t, 30> kDistanceExtra{
    0U, 0U, 0U, 0U, 1U, 1U, 2U, 2U, 3U, 3U,
    4U, 4U, 5U, 5U, 6U, 6U, 7U, 7U, 8U, 8U,
    9U, 9U, 10U, 10U, 11U, 11U, 12U, 12U, 13U, 13U};
constexpr std::array<std::uint8_t, 19> kCodeLengthOrder{
    16U, 17U, 18U, 0U, 8U, 7U, 9U, 6U, 10U, 5U,
    11U, 4U, 12U, 3U, 13U, 2U, 14U, 1U, 15U};

std::uint32_t read_u32_be(const std::span<const std::byte> bytes,
                          const std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[offset]))
            << 24U) |
           (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[offset + 1U]))
            << 16U) |
           (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[offset + 2U]))
            << 8U) |
           static_cast<std::uint32_t>(
               std::to_integer<std::uint8_t>(bytes[offset + 3U]));
}

void append_u32_be(std::vector<std::byte>& bytes,
                   const std::uint32_t value) {
    bytes.push_back(static_cast<std::byte>(value >> 24U));
    bytes.push_back(static_cast<std::byte>(value >> 16U));
    bytes.push_back(static_cast<std::byte>(value >> 8U));
    bytes.push_back(static_cast<std::byte>(value));
}

std::uint32_t crc32_update(std::uint32_t crc,
                           const std::uint8_t value) noexcept {
    crc ^= value;
    for (unsigned int bit = 0U; bit < 8U; ++bit) {
        crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xedb88320U : crc >> 1U;
    }
    return crc;
}

std::uint32_t png_crc(const std::array<std::uint8_t, 4>& type,
                      const std::span<const std::byte> data) noexcept {
    auto crc = 0xffffffffU;
    for (const auto value : type) {
        crc = crc32_update(crc, value);
    }
    for (const auto value : data) {
        crc = crc32_update(crc, std::to_integer<std::uint8_t>(value));
    }
    return ~crc;
}

void append_chunk(std::vector<std::byte>& output,
                  const std::array<std::uint8_t, 4>& type,
                  const std::span<const std::byte> data) {
    append_u32_be(output, static_cast<std::uint32_t>(data.size()));
    for (const auto value : type) {
        output.push_back(static_cast<std::byte>(value));
    }
    output.insert(output.end(), data.begin(), data.end());
    append_u32_be(output, png_crc(type, data));
}

std::uint32_t adler32(const std::span<const std::byte> data) noexcept {
    std::uint32_t sum_a = 1U;
    std::uint32_t sum_b = 0U;
    for (const auto value : data) {
        sum_a = (sum_a + std::to_integer<std::uint8_t>(value)) % 65521U;
        sum_b = (sum_b + sum_a) % 65521U;
    }
    return (sum_b << 16U) | sum_a;
}

bool checked_image_shape(const std::uint32_t width,
                         const std::uint32_t height,
                         std::size_t& pixel_count,
                         std::size_t& row_bytes,
                         std::size_t& raw_bytes) noexcept {
    if (width == 0U || height == 0U) {
        return false;
    }
    const auto pixels = static_cast<std::uint64_t>(width) * height;
    const auto row = static_cast<std::uint64_t>(width) *
                         kRgbaBytesPerPixel;
    if (row == std::numeric_limits<std::uint64_t>::max() ||
        row + 1U > std::numeric_limits<std::uint64_t>::max() / height) {
        return false;
    }
    const auto raw = (row + 1U) * height;
    if (pixels > optical::kMaxMp0SurfacePixels ||
        pixels > std::numeric_limits<std::size_t>::max() ||
        row > std::numeric_limits<std::size_t>::max() ||
        raw > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    pixel_count = static_cast<std::size_t>(pixels);
    row_bytes = static_cast<std::size_t>(row);
    raw_bytes = static_cast<std::size_t>(raw);
    return true;
}

Status encode_zlib_stored(const std::span<const std::byte> raw,
                          std::vector<std::byte>& compressed) {
    try {
        compressed.clear();
        compressed.reserve(raw.size() +
                           ((raw.size() + 65534U) / 65535U) * 5U + 6U);
        compressed.push_back(static_cast<std::byte>(0x78U));
        compressed.push_back(static_cast<std::byte>(0x01U));
        std::size_t offset = 0U;
        while (offset < raw.size()) {
            const auto remaining = raw.size() - offset;
            const auto length = std::min<std::size_t>(remaining, 65535U);
            const auto final_block = offset + length == raw.size();
            compressed.push_back(static_cast<std::byte>(final_block ? 1U : 0U));
            const auto length_u16 = static_cast<std::uint16_t>(length);
            compressed.push_back(static_cast<std::byte>(length_u16));
            compressed.push_back(
                static_cast<std::byte>(length_u16 >> 8U));
            const auto inverse = static_cast<std::uint16_t>(~length_u16);
            compressed.push_back(static_cast<std::byte>(inverse));
            compressed.push_back(static_cast<std::byte>(inverse >> 8U));
            compressed.insert(compressed.end(), raw.begin() + offset,
                              raw.begin() + offset + length);
            offset += length;
        }
        append_u32_be(compressed, adler32(raw));
        return compressed.size() <= kMaxPngEncodedBytes ? Status::ok
                                                        : Status::resource_limit;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

class BitReader {
  public:
    explicit BitReader(const std::span<const std::byte> bytes) : bytes_(bytes) {}

    bool read(const unsigned int count, std::uint32_t& value) noexcept {
        if (count > 32U || bit_position_ > bytes_.size() * 8U ||
            count > bytes_.size() * 8U - bit_position_) {
            return false;
        }
        value = 0U;
        for (unsigned int bit = 0U; bit < count; ++bit) {
            const auto byte_index = (bit_position_ + bit) / 8U;
            const auto bit_index = (bit_position_ + bit) % 8U;
            const auto input = std::to_integer<std::uint8_t>(bytes_[byte_index]);
            value |= static_cast<std::uint32_t>((input >> bit_index) & 1U)
                     << bit;
        }
        bit_position_ += count;
        return true;
    }

    void align() noexcept { bit_position_ = (bit_position_ + 7U) & ~7U; }

    [[nodiscard]] std::size_t byte_position() const noexcept {
        return (bit_position_ + 7U) / 8U;
    }

  private:
    std::span<const std::byte> bytes_;
    std::size_t bit_position_ = 0U;
};

class HuffmanTree {
  public:
    struct Node {
        int child[2] = {-1, -1};
        int symbol = -1;
    };

    bool build(const std::span<const std::uint8_t> lengths) {
        std::array<unsigned int, 16> counts{};
        unsigned int maximum_length = 0U;
        for (const auto length : lengths) {
            if (length > 15U) {
                return false;
            }
            if (length != 0U) {
                ++counts[length];
                maximum_length = std::max(maximum_length,
                                          static_cast<unsigned int>(length));
            }
        }
        if (maximum_length == 0U) {
            return false;
        }

        std::array<unsigned int, 16> next_code{};
        unsigned int code = 0U;
        for (unsigned int bits = 1U; bits <= 15U; ++bits) {
            code = (code + counts[bits - 1U]) << 1U;
            if (code + counts[bits] > (1U << bits)) {
                return false;
            }
            next_code[bits] = code;
        }

        nodes_.clear();
        nodes_.push_back(Node{});
        for (std::size_t symbol = 0U; symbol < lengths.size(); ++symbol) {
            const auto length = lengths[symbol];
            if (length == 0U) {
                continue;
            }
            const auto symbol_code = next_code[length]++;
            auto node_index = 0;
            for (unsigned int bit = 0U; bit < length; ++bit) {
                const auto branch = static_cast<unsigned int>(
                    (symbol_code >> bit) & 1U);
                auto& node = nodes_[static_cast<std::size_t>(node_index)];
                if (node.symbol >= 0) {
                    return false;
                }
                auto child = node.child[branch];
                if (child < 0) {
                    child = static_cast<int>(nodes_.size());
                    node.child[branch] = child;
                    nodes_.push_back(Node{});
                }
                node_index = child;
            }
            auto& leaf = nodes_[static_cast<std::size_t>(node_index)];
            if (leaf.symbol >= 0 || leaf.child[0] >= 0 || leaf.child[1] >= 0) {
                return false;
            }
            leaf.symbol = static_cast<int>(symbol);
        }
        return true;
    }

    bool decode(BitReader& reader, std::uint32_t& symbol) const noexcept {
        if (nodes_.empty()) {
            return false;
        }
        auto node_index = 0;
        for (unsigned int depth = 0U; depth < 15U; ++depth) {
            const auto& node = nodes_[static_cast<std::size_t>(node_index)];
            if (node.symbol >= 0) {
                symbol = static_cast<std::uint32_t>(node.symbol);
                return true;
            }
            std::uint32_t bit = 0U;
            if (!reader.read(1U, bit) || node.child[bit] < 0) {
                return false;
            }
            node_index = node.child[bit];
        }
        const auto& node = nodes_[static_cast<std::size_t>(node_index)];
        if (node.symbol < 0) {
            return false;
        }
        symbol = static_cast<std::uint32_t>(node.symbol);
        return true;
    }

  private:
    std::vector<Node> nodes_;
};

bool build_fixed_trees(HuffmanTree& literal_length,
                       HuffmanTree& distance) {
    std::array<std::uint8_t, 288> literal_lengths{};
    for (std::size_t symbol = 0U; symbol <= 143U; ++symbol) {
        literal_lengths[symbol] = 8U;
    }
    for (std::size_t symbol = 144U; symbol <= 255U; ++symbol) {
        literal_lengths[symbol] = 9U;
    }
    for (std::size_t symbol = 256U; symbol <= 279U; ++symbol) {
        literal_lengths[symbol] = 7U;
    }
    for (std::size_t symbol = 280U; symbol < literal_lengths.size(); ++symbol) {
        literal_lengths[symbol] = 8U;
    }
    std::array<std::uint8_t, 32> distance_lengths{};
    distance_lengths.fill(5U);
    return literal_length.build(literal_lengths) &&
           distance.build(std::span<const std::uint8_t>(
               distance_lengths.data(), distance_lengths.size()));
}

bool build_dynamic_trees(BitReader& reader,
                         HuffmanTree& literal_length,
                         HuffmanTree& distance) {
    std::uint32_t value = 0U;
    if (!reader.read(5U, value)) {
        return false;
    }
    const auto literal_count = value + 257U;
    if (!reader.read(5U, value)) {
        return false;
    }
    const auto distance_count = value + 1U;
    if (!reader.read(4U, value)) {
        return false;
    }
    const auto code_length_count = value + 4U;

    std::array<std::uint8_t, 19> code_length_lengths{};
    for (std::size_t index = 0U; index < code_length_count; ++index) {
        if (!reader.read(3U, value)) {
            return false;
        }
        code_length_lengths[kCodeLengthOrder[index]] =
            static_cast<std::uint8_t>(value);
    }

    HuffmanTree code_length_tree;
    if (!code_length_tree.build(code_length_lengths)) {
        return false;
    }

    std::vector<std::uint8_t> lengths;
    try {
        lengths.reserve(literal_count + distance_count);
        while (lengths.size() < literal_count + distance_count) {
            std::uint32_t symbol = 0U;
            if (!code_length_tree.decode(reader, symbol)) {
                return false;
            }
            if (symbol <= 15U) {
                lengths.push_back(static_cast<std::uint8_t>(symbol));
                continue;
            }
            if (symbol == 16U) {
                if (lengths.empty() || !reader.read(2U, value)) {
                    return false;
                }
                const auto repeat = value + 3U;
                if (repeat > lengths.size() ||
                    lengths.size() + repeat > literal_count + distance_count) {
                    return false;
                }
                const auto previous = lengths.back();
                lengths.insert(lengths.end(), repeat, previous);
                continue;
            }
            if (symbol == 17U || symbol == 18U) {
                const auto extra_bits = symbol == 17U ? 3U : 7U;
                const auto base = symbol == 17U ? 3U : 11U;
                if (!reader.read(extra_bits, value)) {
                    return false;
                }
                const auto repeat = value + base;
                if (lengths.size() + repeat >
                    literal_count + distance_count) {
                    return false;
                }
                lengths.insert(lengths.end(), repeat, 0U);
                continue;
            }
            return false;
        }
    } catch (const std::bad_alloc&) {
        return false;
    }

    return literal_length.build(std::span<const std::uint8_t>(
               lengths.data(), literal_count)) &&
           distance.build(std::span<const std::uint8_t>(
               lengths.data() + literal_count, distance_count));
}

bool append_inflated_byte(std::vector<std::byte>& output,
                          const std::uint8_t value,
                          const std::size_t expected_size) {
    if (output.size() >= expected_size) {
        return false;
    }
    output.push_back(static_cast<std::byte>(value));
    return true;
}

bool inflate_deflate(const std::span<const std::byte> compressed,
                     const std::size_t expected_size,
                     std::vector<std::byte>& output) {
    BitReader reader(compressed);
    bool final_block = false;
    try {
        while (!final_block) {
            std::uint32_t value = 0U;
            if (!reader.read(1U, value)) {
                return false;
            }
            final_block = value != 0U;
            if (!reader.read(2U, value)) {
                return false;
            }
            const auto block_type = value;
            if (block_type == 0U) {
                reader.align();
                std::uint32_t length = 0U;
                std::uint32_t inverse = 0U;
                if (!reader.read(16U, length) || !reader.read(16U, inverse) ||
                    ((length ^ inverse) & 0xffffU) != 0xffffU) {
                    return false;
                }
                for (std::uint32_t index = 0U; index < length; ++index) {
                    if (!reader.read(8U, value) ||
                        !append_inflated_byte(output,
                                              static_cast<std::uint8_t>(value),
                                              expected_size)) {
                        return false;
                    }
                }
                continue;
            }
            if (block_type == 3U) {
                return false;
            }

            HuffmanTree literal_length;
            HuffmanTree distance;
            if (block_type == 1U) {
                if (!build_fixed_trees(literal_length, distance)) {
                    return false;
                }
            } else if (!build_dynamic_trees(reader, literal_length, distance)) {
                return false;
            }

            bool end_of_block = false;
            while (!end_of_block) {
                std::uint32_t symbol = 0U;
                if (!literal_length.decode(reader, symbol)) {
                    return false;
                }
                if (symbol < 256U) {
                    if (!append_inflated_byte(
                            output, static_cast<std::uint8_t>(symbol),
                            expected_size)) {
                        return false;
                    }
                    continue;
                }
                if (symbol == 256U) {
                    end_of_block = true;
                    continue;
                }
                if (symbol < 257U || symbol > 285U) {
                    return false;
                }
                const auto length_index = symbol - 257U;
                const auto length_extra = kLengthExtra[length_index];
                if (!reader.read(length_extra, value)) {
                    return false;
                }
                const auto length = kLengthBase[length_index] + value;

                if (!distance.decode(reader, symbol) || symbol >= 30U) {
                    return false;
                }
                const auto distance_extra = kDistanceExtra[symbol];
                if (!reader.read(distance_extra, value)) {
                    return false;
                }
                const auto distance_value = kDistanceBase[symbol] + value;
                if (distance_value == 0U || distance_value > output.size()) {
                    return false;
                }
                for (std::uint32_t index = 0U; index < length; ++index) {
                    const auto source = output.size() - distance_value;
                    const auto byte = std::to_integer<std::uint8_t>(
                        output[source]);
                    if (!append_inflated_byte(output, byte, expected_size)) {
                        return false;
                    }
                }
            }
        }
    } catch (const std::bad_alloc&) {
        return false;
    }
    reader.align();
    return final_block && reader.byte_position() == compressed.size() &&
           output.size() == expected_size;
}

std::uint32_t paeth(const std::uint32_t left,
                    const std::uint32_t above,
                    const std::uint32_t upper_left) noexcept {
    const auto prediction = static_cast<int>(left) +
                            static_cast<int>(above) -
                            static_cast<int>(upper_left);
    const auto left_distance = std::abs(prediction - static_cast<int>(left));
    const auto above_distance =
        std::abs(prediction - static_cast<int>(above));
    const auto upper_left_distance =
        std::abs(prediction - static_cast<int>(upper_left));
    if (left_distance <= above_distance &&
        left_distance <= upper_left_distance) {
        return left;
    }
    if (above_distance <= upper_left_distance) {
        return above;
    }
    return upper_left;
}

Status decode_zlib(const std::span<const std::byte> compressed,
                   const std::size_t expected_size,
                   std::vector<std::byte>& raw) {
    if (compressed.size() < 6U) {
        return Status::integrity;
    }
    const auto cmf = std::to_integer<std::uint8_t>(compressed[0U]);
    const auto flg = std::to_integer<std::uint8_t>(compressed[1U]);
    if ((cmf & 0x0fU) != 8U || (cmf >> 4U) > 7U || (flg & 0x20U) != 0U ||
        ((static_cast<std::uint16_t>(cmf) << 8U) | flg) % 31U != 0U) {
        return Status::protocol;
    }

    const auto deflate = compressed.subspan(2U, compressed.size() - 6U);
    try {
        std::vector<std::byte> candidate;
        candidate.reserve(expected_size);
        if (!inflate_deflate(deflate, expected_size, candidate)) {
            return Status::integrity;
        }
        const auto expected_adler =
            read_u32_be(compressed, compressed.size() - 4U);
        if (adler32(candidate) != expected_adler) {
            return Status::integrity;
        }
        raw = std::move(candidate);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

}  // namespace

Status encode_rgba8_png(const std::uint32_t width,
                        const std::uint32_t height,
                        const std::span<const optical::Rgba8> pixels,
                        std::vector<std::byte>& encoded) {
    std::size_t pixel_count = 0U;
    std::size_t row_bytes = 0U;
    std::size_t raw_bytes = 0U;
    if (!checked_image_shape(width, height, pixel_count, row_bytes,
                             raw_bytes) || pixels.size() != pixel_count) {
        return pixels.size() != pixel_count ? Status::invalid_argument
                                            : Status::resource_limit;
    }

    try {
        std::vector<std::byte> raw(raw_bytes, std::byte{0});
        for (std::uint32_t row = 0U; row < height; ++row) {
            const auto raw_row = static_cast<std::size_t>(row) *
                                 (row_bytes + 1U);
            raw[raw_row] = std::byte{0};
            for (std::uint32_t column = 0U; column < width; ++column) {
                const auto& pixel = pixels[static_cast<std::size_t>(row) *
                                            width + column];
                const auto offset = raw_row + 1U +
                                    static_cast<std::size_t>(column) *
                                        kRgbaBytesPerPixel;
                raw[offset] = static_cast<std::byte>(pixel.red);
                raw[offset + 1U] = static_cast<std::byte>(pixel.green);
                raw[offset + 2U] = static_cast<std::byte>(pixel.blue);
                raw[offset + 3U] = static_cast<std::byte>(pixel.alpha);
            }
        }

        std::vector<std::byte> compressed;
        const auto compression_status = encode_zlib_stored(raw, compressed);
        if (compression_status != Status::ok) {
            return compression_status;
        }

        std::vector<std::byte> candidate;
        candidate.reserve(kPngSignature.size() + 25U +
                          compressed.size() + 12U + 12U);
        for (const auto value : kPngSignature) {
            candidate.push_back(static_cast<std::byte>(value));
        }

        std::array<std::byte, 13> ihdr{};
        ihdr[0U] = static_cast<std::byte>(width >> 24U);
        ihdr[1U] = static_cast<std::byte>(width >> 16U);
        ihdr[2U] = static_cast<std::byte>(width >> 8U);
        ihdr[3U] = static_cast<std::byte>(width);
        ihdr[4U] = static_cast<std::byte>(height >> 24U);
        ihdr[5U] = static_cast<std::byte>(height >> 16U);
        ihdr[6U] = static_cast<std::byte>(height >> 8U);
        ihdr[7U] = static_cast<std::byte>(height);
        ihdr[8U] = static_cast<std::byte>(8U);
        ihdr[9U] = static_cast<std::byte>(6U);
        append_chunk(candidate, {'I', 'H', 'D', 'R'}, ihdr);
        append_chunk(candidate, {'I', 'D', 'A', 'T'}, compressed);
        append_chunk(candidate, {'I', 'E', 'N', 'D'}, {});
        if (candidate.size() > kMaxPngEncodedBytes) {
            return Status::resource_limit;
        }
        encoded = std::move(candidate);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status decode_rgba8_png(const std::span<const std::byte> encoded,
                        std::uint32_t& width,
                        std::uint32_t& height,
                        std::vector<optical::Rgba8>& pixels) {
    if (encoded.size() > kMaxPngEncodedBytes || encoded.size() < 8U ||
        !std::equal(kPngSignature.begin(), kPngSignature.end(),
                    encoded.begin(), [](const auto expected, const auto value) {
                        return expected ==
                               std::to_integer<std::uint8_t>(value);
                    })) {
        return Status::protocol;
    }

    try {
        std::vector<std::byte> compressed;
        std::uint32_t parsed_width = 0U;
        std::uint32_t parsed_height = 0U;
        bool saw_header = false;
        bool saw_data = false;
        bool saw_end = false;
        std::size_t offset = kPngSignature.size();

        while (offset < encoded.size()) {
            if (encoded.size() - offset < 12U) {
                return Status::protocol;
            }
            const auto length = read_u32_be(encoded, offset);
            const auto chunk_size = static_cast<std::size_t>(length);
            if (chunk_size > encoded.size() - offset - 12U) {
                return Status::protocol;
            }
            const auto type_offset = offset + 4U;
            const auto data_offset = offset + 8U;
            const auto crc_offset = data_offset + chunk_size;
            const std::array<std::uint8_t, 4> type{
                std::to_integer<std::uint8_t>(encoded[type_offset]),
                std::to_integer<std::uint8_t>(encoded[type_offset + 1U]),
                std::to_integer<std::uint8_t>(encoded[type_offset + 2U]),
                std::to_integer<std::uint8_t>(encoded[type_offset + 3U])};
            const auto data = encoded.subspan(data_offset, chunk_size);
            if (png_crc(type, data) != read_u32_be(encoded, crc_offset)) {
                return Status::integrity;
            }

            if (type == std::array<std::uint8_t, 4>{'I', 'H', 'D', 'R'}) {
                if (saw_header || chunk_size != 13U || offset != 8U) {
                    return Status::protocol;
                }
                parsed_width = read_u32_be(data, 0U);
                parsed_height = read_u32_be(data, 4U);
                if (data[8U] != std::byte{8} ||
                    data[9U] != std::byte{6} || data[10U] != std::byte{0} ||
                    data[11U] != std::byte{0} || data[12U] != std::byte{0}) {
                    return Status::unsupported;
                }
                std::size_t ignored_pixels = 0U;
                std::size_t ignored_row = 0U;
                std::size_t ignored_raw = 0U;
                if (!checked_image_shape(parsed_width, parsed_height,
                                         ignored_pixels, ignored_row,
                                         ignored_raw)) {
                    return Status::resource_limit;
                }
                saw_header = true;
            } else if (type == std::array<std::uint8_t, 4>{'I', 'D', 'A', 'T'}) {
                if (!saw_header || saw_end) {
                    return Status::protocol;
                }
                if (compressed.size() > kMaxPngEncodedBytes - chunk_size) {
                    return Status::resource_limit;
                }
                compressed.insert(compressed.end(), data.begin(), data.end());
                saw_data = true;
            } else if (type == std::array<std::uint8_t, 4>{'I', 'E', 'N', 'D'}) {
                if (!saw_header || !saw_data || chunk_size != 0U) {
                    return Status::protocol;
                }
                saw_end = true;
            } else {
                // Unknown ancillary chunks are ignored; unknown critical
                // chunks cannot be safely interpreted by this decoder.
                if ((type[0] & 0x20U) == 0U) {
                    return Status::unsupported;
                }
            }

            offset = crc_offset + 4U;
            if (saw_end) {
                break;
            }
        }

        if (!saw_header || !saw_data || !saw_end || offset != encoded.size()) {
            return Status::protocol;
        }

        std::size_t pixel_count = 0U;
        std::size_t row_bytes = 0U;
        std::size_t raw_bytes = 0U;
        if (!checked_image_shape(parsed_width, parsed_height, pixel_count,
                                 row_bytes, raw_bytes)) {
            return Status::resource_limit;
        }
        std::vector<std::byte> raw;
        const auto inflate_status = decode_zlib(compressed, raw_bytes, raw);
        if (inflate_status != Status::ok) {
            return inflate_status;
        }

        std::vector<std::uint8_t> previous(row_bytes, 0U);
        std::vector<std::uint8_t> current(row_bytes, 0U);
        std::vector<optical::Rgba8> candidate(pixel_count);
        std::size_t raw_offset = 0U;
        for (std::uint32_t row = 0U; row < parsed_height; ++row) {
            const auto filter = std::to_integer<std::uint8_t>(raw[raw_offset++]);
            for (std::size_t column = 0U; column < row_bytes; ++column) {
                const auto input =
                    std::to_integer<std::uint8_t>(raw[raw_offset + column]);
                const auto left = column >= kRgbaBytesPerPixel
                                      ? current[column - kRgbaBytesPerPixel]
                                      : 0U;
                const auto above = previous[column];
                const auto upper_left = column >= kRgbaBytesPerPixel
                                            ? previous[column -
                                                       kRgbaBytesPerPixel]
                                            : 0U;
                std::uint32_t reconstructed = input;
                switch (filter) {
                    case 0U:
                        break;
                    case 1U:
                        reconstructed += left;
                        break;
                    case 2U:
                        reconstructed += above;
                        break;
                    case 3U:
                        reconstructed += (left + above) / 2U;
                        break;
                    case 4U:
                        reconstructed += paeth(left, above, upper_left);
                        break;
                    default:
                        return Status::unsupported;
                }
                current[column] = static_cast<std::uint8_t>(reconstructed);
            }
            raw_offset += row_bytes;
            for (std::uint32_t column = 0U; column < parsed_width; ++column) {
                const auto source = static_cast<std::size_t>(column) *
                                    kRgbaBytesPerPixel;
                candidate[static_cast<std::size_t>(row) * parsed_width +
                         column] = optical::Rgba8{
                    current[source], current[source + 1U], current[source + 2U],
                    current[source + 3U]};
            }
            std::swap(previous, current);
        }
        if (raw_offset != raw.size()) {
            return Status::integrity;
        }
        width = parsed_width;
        height = parsed_height;
        pixels = std::move(candidate);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

}  // namespace glyph::image
