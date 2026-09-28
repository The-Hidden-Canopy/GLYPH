#include "glyph/phy/rgb8.hpp"

#include <cmath>
#include <cstddef>
#include <limits>

namespace glyph {
namespace {

struct ChannelDecision {
    bool bit = false;
    bool erasure = false;
    float confidence = 0.0F;
};

ChannelDecision classify_channel(const float observation,
                                 const float black,
                                 const float one,
                                 const float min_confidence) {
    if (!std::isfinite(observation)) {
        return ChannelDecision{false, true, 0.0F};
    }
    const auto normalized = (observation - black) / (one - black);
    if (!std::isfinite(normalized)) {
        return ChannelDecision{false, true, 0.0F};
    }
    const auto confidence = std::fabs((normalized * 2.0F) - 1.0F);
    return ChannelDecision{normalized >= 0.5F,
                           confidence < min_confidence,
                           confidence};
}

std::size_t required_cells(const std::uint64_t bytes) {
    const auto bits = bytes * 8U;
    return static_cast<std::size_t>((bits + 2U) / 3U);
}

}  // namespace

Status encode_rgb8_payload(const std::span<const std::byte> payload,
                           std::vector<Rgb8Cell>& cells,
                           const std::size_t max_payload_bytes) {
    if (payload.size() > max_payload_bytes ||
        payload.size() > kMaxRgb8PayloadBytes) {
        return Status::resource_limit;
    }
    const auto cell_count = required_cells(payload.size());
    try {
        cells.assign(cell_count, Rgb8Cell{});
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }

    const auto total_bits = payload.size() * 8U;
    for (std::size_t cell = 0U; cell < cell_count; ++cell) {
        std::uint8_t symbol = 0U;
        for (unsigned int bit = 0U; bit < 3U; ++bit) {
            const auto bit_index = cell * 3U + bit;
            symbol = static_cast<std::uint8_t>(symbol << 1U);
            if (bit_index < total_bits) {
                const auto byte_index = bit_index / 8U;
                const auto bit_in_byte = 7U - (bit_index % 8U);
                const auto byte = std::to_integer<std::uint8_t>(payload[byte_index]);
                symbol = static_cast<std::uint8_t>(
                    symbol | ((byte >> bit_in_byte) & 1U));
            }
        }
        cells[cell] = Rgb8Cell{
            static_cast<std::uint8_t>((symbol & 0x04U) != 0U ? 0xffU : 0U),
            static_cast<std::uint8_t>((symbol & 0x02U) != 0U ? 0xffU : 0U),
            static_cast<std::uint8_t>((symbol & 0x01U) != 0U ? 0xffU : 0U)};
    }
    return Status::ok;
}

Status decode_rgb8_payload(const std::span<const Rgb8Cell> cells,
                           const std::uint64_t expected_bytes,
                           std::vector<std::byte>& payload,
                           const std::size_t max_payload_bytes) {
    if (expected_bytes > max_payload_bytes ||
        expected_bytes > kMaxRgb8PayloadBytes ||
        expected_bytes > (std::numeric_limits<std::size_t>::max() / 8U)) {
        return Status::resource_limit;
    }
    const auto cell_count = required_cells(expected_bytes);
    if (cells.size() != cell_count) {
        return Status::invalid_argument;
    }
    try {
        payload.assign(static_cast<std::size_t>(expected_bytes), std::byte{0});
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }

    const auto total_bits = static_cast<std::size_t>(expected_bytes) * 8U;
    for (std::size_t cell = 0U; cell < cells.size(); ++cell) {
        const auto& value = cells[cell];
        if ((value.red != 0U && value.red != 0xffU) ||
            (value.green != 0U && value.green != 0xffU) ||
            (value.blue != 0U && value.blue != 0xffU)) {
            return Status::integrity;
        }
        const auto symbol = static_cast<std::uint8_t>(
            ((value.red == 0xffU) ? 0x04U : 0U) |
            ((value.green == 0xffU) ? 0x02U : 0U) |
            ((value.blue == 0xffU) ? 0x01U : 0U));
        for (unsigned int bit = 0U; bit < 3U; ++bit) {
            const auto bit_index = cell * 3U + bit;
            const auto bit_value = static_cast<std::uint8_t>(
                (symbol >> (2U - bit)) & 1U);
            if (bit_index < total_bits) {
                const auto byte_index = bit_index / 8U;
                const auto bit_in_byte = 7U - (bit_index % 8U);
                auto byte = std::to_integer<std::uint8_t>(payload[byte_index]);
                byte = static_cast<std::uint8_t>(
                    byte | (bit_value << bit_in_byte));
                payload[byte_index] = static_cast<std::byte>(byte);
            } else if (bit_value != 0U) {
                return Status::integrity;
            }
        }
    }
    return Status::ok;
}

Status classify_rgb8_cell(const Rgb8Observation& observation,
                          const Rgb8Calibration& calibration,
                          Rgb8Decision& decision) {
    if (!std::isfinite(calibration.black.red) ||
        !std::isfinite(calibration.black.green) ||
        !std::isfinite(calibration.black.blue) ||
        !std::isfinite(calibration.one.red) ||
        !std::isfinite(calibration.one.green) ||
        !std::isfinite(calibration.one.blue) ||
        !std::isfinite(calibration.min_confidence) ||
        calibration.min_confidence < 0.0F ||
        calibration.min_confidence > 1.0F ||
        calibration.one.red <= calibration.black.red ||
        calibration.one.green <= calibration.black.green ||
        calibration.one.blue <= calibration.black.blue) {
        return Status::invalid_argument;
    }

    const auto red = classify_channel(observation.red, calibration.black.red,
                                      calibration.one.red,
                                      calibration.min_confidence);
    const auto green = classify_channel(observation.green,
                                        calibration.black.green,
                                        calibration.one.green,
                                        calibration.min_confidence);
    const auto blue = classify_channel(observation.blue, calibration.black.blue,
                                       calibration.one.blue,
                                       calibration.min_confidence);
    decision.symbol = static_cast<std::uint8_t>(
        (red.bit ? 0x04U : 0U) | (green.bit ? 0x02U : 0U) |
        (blue.bit ? 0x01U : 0U));
    decision.erasure = red.erasure || green.erasure || blue.erasure;
    decision.confidence = {red.confidence, green.confidence, blue.confidence};
    return Status::ok;
}

}  // namespace glyph

