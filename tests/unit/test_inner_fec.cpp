#include "glyph/fec/inner_reed_solomon.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

int main() {
    glyph::InnerRsConfig config;
    config.data_bytes = 16U;
    config.parity_bytes = 8U;

    glyph::InnerReedSolomon code;
    assert(glyph::InnerReedSolomon::create(config, code) == glyph::Status::ok);
    assert(code.codeword_bytes() == 24U);

    std::vector<std::byte> data(config.data_bytes);
    for (std::size_t index = 0U; index < data.size(); ++index) {
        data[index] = static_cast<std::byte>((index * 29U + 7U) & 0xffU);
    }

    std::vector<std::byte> codeword;
    assert(code.encode(data, codeword) == glyph::Status::ok);
    assert(codeword.size() == code.codeword_bytes());
    const std::array<std::uint8_t, 24> expected_codeword{
        0x07U, 0x24U, 0x41U, 0x5eU, 0x7bU, 0x98U, 0xb5U, 0xd2U,
        0xefU, 0x0cU, 0x29U, 0x46U, 0x63U, 0x80U, 0x9dU, 0xbaU,
        0xa5U, 0x6eU, 0xbaU, 0x83U, 0x73U, 0xb2U, 0x2eU, 0xedU};
    for (std::size_t index = 0U; index < codeword.size(); ++index) {
        assert(std::to_integer<std::uint8_t>(codeword[index]) ==
               expected_codeword[index]);
    }

    auto clean = codeword;
    assert(code.decode(clean) == glyph::Status::ok);
    assert(clean == codeword);

    auto one_error = codeword;
    one_error[3U] ^= std::byte{0x55};
    assert(code.decode(one_error) == glyph::Status::ok);
    assert(one_error == codeword);

    auto four_errors = codeword;
    four_errors[0U] ^= std::byte{0x01};
    four_errors[6U] ^= std::byte{0x22};
    four_errors[13U] ^= std::byte{0x33};
    four_errors[23U] ^= std::byte{0x44};
    assert(code.decode(four_errors) == glyph::Status::ok);
    assert(four_errors == codeword);

    auto erasures = codeword;
    const std::array<std::uint16_t, 8> erased_positions{
        0U, 2U, 5U, 8U, 11U, 16U, 20U, 23U};
    for (const auto position : erased_positions) {
        erasures[position] = std::byte{0};
    }
    assert(code.recover_erasures(erasures, erased_positions) ==
           glyph::Status::ok);
    assert(erasures == codeword);

    glyph::InnerRsConfig default_config;
    glyph::InnerReedSolomon default_code;
    assert(glyph::InnerReedSolomon::create(default_config, default_code) ==
           glyph::Status::ok);
    std::vector<std::byte> default_data(default_config.data_bytes);
    for (std::size_t index = 0U; index < default_data.size(); ++index) {
        default_data[index] =
            static_cast<std::byte>((index * 13U + 0x31U) & 0xffU);
    }
    std::vector<std::byte> default_codeword;
    assert(default_code.encode(default_data, default_codeword) ==
           glyph::Status::ok);
    const std::array<std::uint16_t, 16> default_error_positions{
        0U, 1U, 17U, 33U, 64U, 95U, 127U, 159U,
        191U, 207U, 223U, 230U, 237U, 244U, 251U, 254U};
    const std::array<std::uint8_t, 16> default_error_values{
        0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x20U, 0x40U, 0x80U,
        0x03U, 0x06U, 0x0cU, 0x18U, 0x30U, 0x60U, 0xc0U, 0xa5U};
    auto default_corrupted = default_codeword;
    for (std::size_t index = 0U; index < default_error_positions.size();
         ++index) {
        default_corrupted[default_error_positions[index]] ^=
            static_cast<std::byte>(default_error_values[index]);
    }
    assert(default_code.decode(default_corrupted) == glyph::Status::ok);
    assert(default_corrupted == default_codeword);

    auto corrupted_present = codeword;
    corrupted_present[1U] ^= std::byte{0x7f};
    corrupted_present[4U] = std::byte{0};
    const auto corrupted_present_original = corrupted_present;
    assert(code.recover_erasures(
               corrupted_present,
               std::span<const std::uint16_t>(erased_positions).first(1U)) ==
           glyph::Status::integrity);
    assert(corrupted_present == corrupted_present_original);

    const std::array<std::uint16_t, 9> too_many_erasures{
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
    auto too_many_erasure_bytes = codeword;
    const auto too_many_erasure_original = too_many_erasure_bytes;
    assert(code.recover_erasures(too_many_erasure_bytes, too_many_erasures) ==
           glyph::Status::integrity);
    assert(too_many_erasure_bytes == too_many_erasure_original);

    const std::array<std::uint16_t, 1> out_of_range_erasure{24U};
    assert(code.recover_erasures(codeword, out_of_range_erasure) ==
           glyph::Status::invalid_argument);

    auto too_many_errors = codeword;
    too_many_errors[0U] ^= std::byte{0x01};
    too_many_errors[1U] ^= std::byte{0x02};
    too_many_errors[2U] ^= std::byte{0x04};
    too_many_errors[3U] ^= std::byte{0x08};
    too_many_errors[4U] ^= std::byte{0x10};
    const auto too_many_original = too_many_errors;
    assert(code.decode(too_many_errors) == glyph::Status::integrity);
    assert(too_many_errors == too_many_original);

    const std::array<std::uint16_t, 2> duplicate_positions{1U, 1U};
    assert(code.recover_erasures(erasures, duplicate_positions) ==
           glyph::Status::invalid_argument);

    glyph::InnerRsConfig invalid = config;
    invalid.data_bytes = 254U;
    invalid.parity_bytes = 2U;
    assert(glyph::InnerReedSolomon::create(invalid, code) ==
           glyph::Status::invalid_argument);
    invalid = config;
    invalid.parity_bytes = 1U;
    assert(glyph::InnerReedSolomon::create(invalid, code) ==
           glyph::Status::invalid_argument);

    return 0;
}
