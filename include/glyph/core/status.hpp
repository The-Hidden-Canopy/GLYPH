#pragma once

#include <cstdint>

namespace glyph {

enum class Status : std::uint8_t {
    ok = 0,
    invalid_argument,
    io,
    protocol,
    integrity,
    crypto,
    camera,
    display,
    resource_limit,
    unsupported,
};

[[nodiscard]] constexpr bool succeeded(const Status status) noexcept {
    return status == Status::ok;
}

[[nodiscard]] const char* status_name(Status status) noexcept;

}  // namespace glyph

