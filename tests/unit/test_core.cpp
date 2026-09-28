#include "glyph/core/status.hpp"
#include "glyph/crypto/sha256.hpp"
#include "glyph/core/version.hpp"

#include <cassert>
#include <span>
#include <string_view>

int main() {
    using glyph::Status;

    assert(glyph::succeeded(Status::ok));
    assert(!glyph::succeeded(Status::integrity));
    assert(std::string_view{glyph::status_name(Status::resource_limit)} ==
           "resource_limit");
    assert(std::string_view{glyph::status_name(Status::unsupported)} ==
           "unsupported");
    const auto invalid_status = static_cast<Status>(0xff);
    assert(!glyph::succeeded(invalid_status));
    assert(std::string_view{glyph::status_name(invalid_status)} == "unknown");
    assert(std::string_view{glyph::protocol_name} == "glyph/1");
    assert(glyph::protocol_major == 1);
    assert(glyph::protocol_minor == 0);

    const std::string_view abc = "abc";
    const auto abc_bytes = std::as_bytes(std::span<const char>(abc.data(), abc.size()));
    assert(glyph::digest_hex(glyph::Sha256::hash(abc_bytes)) ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    glyph::Sha256 streaming;
    const std::string_view first = "a";
    const std::string_view second = "bc";
    assert(streaming.update(std::as_bytes(
               std::span<const char>(first.data(), first.size()))) == glyph::Status::ok);
    assert(streaming.update(std::as_bytes(
               std::span<const char>(second.data(), second.size()))) == glyph::Status::ok);
    glyph::Digest256 streaming_digest{};
    assert(streaming.finalize(streaming_digest) == glyph::Status::ok);
    assert(glyph::digest_hex(streaming_digest) ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(streaming.update(abc_bytes) == glyph::Status::invalid_argument);

    return 0;
}
