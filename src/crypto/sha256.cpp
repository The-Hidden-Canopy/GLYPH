#include "glyph/crypto/sha256.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace glyph {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

constexpr std::uint32_t rotr(const std::uint32_t value,
                             const unsigned int shift) noexcept {
    return (value >> shift) | (value << (32U - shift));
}

constexpr std::uint32_t choose(const std::uint32_t x,
                               const std::uint32_t y,
                               const std::uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}

constexpr std::uint32_t majority(const std::uint32_t x,
                                 const std::uint32_t y,
                                 const std::uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

constexpr std::uint32_t big_sigma0(const std::uint32_t value) noexcept {
    return rotr(value, 2U) ^ rotr(value, 13U) ^ rotr(value, 22U);
}

constexpr std::uint32_t big_sigma1(const std::uint32_t value) noexcept {
    return rotr(value, 6U) ^ rotr(value, 11U) ^ rotr(value, 25U);
}

constexpr std::uint32_t small_sigma0(const std::uint32_t value) noexcept {
    return rotr(value, 7U) ^ rotr(value, 18U) ^ (value >> 3U);
}

constexpr std::uint32_t small_sigma1(const std::uint32_t value) noexcept {
    return rotr(value, 17U) ^ rotr(value, 19U) ^ (value >> 10U);
}

void append_hex_byte(std::string& output, const std::uint8_t value) {
    constexpr char hex[] = "0123456789abcdef";
    output.push_back(hex[(value >> 4U) & 0x0fU]);
    output.push_back(hex[value & 0x0fU]);
}

}  // namespace

Sha256::Sha256() noexcept
    : state_{0x6a09e667U,
             0xbb67ae85U,
             0x3c6ef372U,
             0xa54ff53aU,
             0x510e527fU,
             0x9b05688cU,
             0x1f83d9abU,
             0x5be0cd19U} {}

Status Sha256::update(const std::span<const std::byte> data) noexcept {
    if (finalized_) {
        return Status::invalid_argument;
    }

    if (data.size() >
        (std::numeric_limits<std::uint64_t>::max() - total_bytes_)) {
        return Status::resource_limit;
    }

    total_bytes_ += static_cast<std::uint64_t>(data.size());
    auto* input = reinterpret_cast<const std::uint8_t*>(data.data());
    std::size_t remaining = data.size();

    if (buffer_size_ != 0U) {
        const auto needed = 64U - buffer_size_;
        const auto copied = std::min(needed, remaining);
        std::copy_n(input, copied, buffer_.data() + buffer_size_);
        buffer_size_ += copied;
        input += copied;
        remaining -= copied;
        if (buffer_size_ == 64U) {
            transform(buffer_.data());
            buffer_size_ = 0U;
        }
    }

    while (remaining >= 64U) {
        transform(input);
        input += 64U;
        remaining -= 64U;
    }

    if (remaining != 0U) {
        std::copy_n(input, remaining, buffer_.data());
        buffer_size_ = remaining;
    }

    return Status::ok;
}

Status Sha256::finalize(Digest256& digest) noexcept {
    if (finalized_) {
        digest = digest_;
        return Status::ok;
    }

    if (total_bytes_ > (std::numeric_limits<std::uint64_t>::max() / 8U)) {
        return Status::resource_limit;
    }

    const auto bit_length = total_bytes_ * 8U;
    buffer_[buffer_size_++] = 0x80U;

    if (buffer_size_ > 56U) {
        std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_),
                  buffer_.end(), 0U);
        transform(buffer_.data());
        buffer_size_ = 0U;
    }

    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_),
              buffer_.begin() + 56, 0U);
    for (unsigned int index = 0U; index < 8U; ++index) {
        buffer_[56U + index] = static_cast<std::uint8_t>(
            bit_length >> (56U - (index * 8U)));
    }
    transform(buffer_.data());
    buffer_size_ = 0U;

    for (unsigned int index = 0U; index < state_.size(); ++index) {
        digest_.bytes[index * 4U] =
            static_cast<std::uint8_t>(state_[index] >> 24U);
        digest_.bytes[index * 4U + 1U] =
            static_cast<std::uint8_t>(state_[index] >> 16U);
        digest_.bytes[index * 4U + 2U] =
            static_cast<std::uint8_t>(state_[index] >> 8U);
        digest_.bytes[index * 4U + 3U] =
            static_cast<std::uint8_t>(state_[index]);
    }

    finalized_ = true;
    digest = digest_;
    return Status::ok;
}

Digest256 Sha256::hash(const std::span<const std::byte> data) noexcept {
    Sha256 hasher;
    Digest256 digest{};
    if (hasher.update(data) == Status::ok) {
        (void)hasher.finalize(digest);
    }
    return digest;
}

void Sha256::transform(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> words{};
    for (unsigned int index = 0U; index < 16U; ++index) {
        const auto offset = index * 4U;
        words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                       (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                       (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                       static_cast<std::uint32_t>(block[offset + 3U]);
    }
    for (unsigned int index = 16U; index < words.size(); ++index) {
        words[index] = small_sigma1(words[index - 2U]) + words[index - 7U] +
                       small_sigma0(words[index - 15U]) + words[index - 16U];
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];

    for (unsigned int index = 0U; index < words.size(); ++index) {
        const auto t1 = h + big_sigma1(e) + choose(e, f, g) +
                        kRoundConstants[index] + words[index];
        const auto t2 = big_sigma0(a) + majority(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

std::string digest_hex(const Digest256& digest) {
    std::string output;
    output.reserve(digest.bytes.size() * 2U);
    for (const auto byte : digest.bytes) {
        append_hex_byte(output, byte);
    }
    return output;
}

}  // namespace glyph

