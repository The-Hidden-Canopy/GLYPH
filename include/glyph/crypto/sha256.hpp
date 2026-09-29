#pragma once

#include "glyph/core/status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace glyph {

struct Digest256 {
    std::array<std::uint8_t, 32> bytes{};

    friend constexpr bool operator==(const Digest256&, const Digest256&) =
        default;
};

[[nodiscard]] std::string digest_hex(const Digest256& digest);

class Sha256 final {
public:
    Sha256() noexcept;

    [[nodiscard]] Status update(std::span<const std::byte> data) noexcept;
    [[nodiscard]] Status finalize(Digest256& digest) noexcept;

    [[nodiscard]] static Digest256 hash(std::span<const std::byte> data) noexcept;

private:
    void transform(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t total_bytes_ = 0;
    std::size_t buffer_size_ = 0;
    bool finalized_ = false;
    Digest256 digest_{};
};

}  // namespace glyph

