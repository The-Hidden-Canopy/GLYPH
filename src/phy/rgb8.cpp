#include "glyph/phy/rgb8.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <utility>

namespace glyph {
namespace {

struct ChannelDecision {
    bool bit = false;
    bool erasure = false;
    float confidence = 0.0F;
};

using Matrix3 = std::array<std::array<float, 3>, 3>;

constexpr float kMinimumRelativeDeterminant = 1.0e-4F;
constexpr float kMaximumInverseGain = 1.0e4F;

bool finite_observation(const Rgb8Observation& observation) {
    return std::isfinite(observation.red) &&
           std::isfinite(observation.green) &&
           std::isfinite(observation.blue);
}

bool invert_matrix(const Matrix3& input, Matrix3& inverse) {
    const auto determinant =
        input[0][0] * (input[1][1] * input[2][2] -
                       input[1][2] * input[2][1]) -
        input[0][1] * (input[1][0] * input[2][2] -
                       input[1][2] * input[2][0]) +
        input[0][2] * (input[1][0] * input[2][1] -
                       input[1][1] * input[2][0]);
    float column_scale = 1.0F;
    for (std::size_t column = 0U; column < 3U; ++column) {
        const auto norm = std::sqrt(
            input[0][column] * input[0][column] +
            input[1][column] * input[1][column] +
            input[2][column] * input[2][column]);
        if (!std::isfinite(norm) || norm == 0.0F) {
            return false;
        }
        column_scale *= norm;
    }
    if (!std::isfinite(determinant) || !std::isfinite(column_scale) ||
        std::fabs(determinant) <
            std::max(1.0e-6F, column_scale * kMinimumRelativeDeterminant)) {
        return false;
    }

    inverse[0][0] = (input[1][1] * input[2][2] -
                     input[1][2] * input[2][1]) /
                    determinant;
    inverse[0][1] = (input[0][2] * input[2][1] -
                     input[0][1] * input[2][2]) /
                    determinant;
    inverse[0][2] = (input[0][1] * input[1][2] -
                     input[0][2] * input[1][1]) /
                    determinant;
    inverse[1][0] = (input[1][2] * input[2][0] -
                     input[1][0] * input[2][2]) /
                    determinant;
    inverse[1][1] = (input[0][0] * input[2][2] -
                     input[0][2] * input[2][0]) /
                    determinant;
    inverse[1][2] = (input[0][2] * input[1][0] -
                     input[0][0] * input[1][2]) /
                    determinant;
    inverse[2][0] = (input[1][0] * input[2][1] -
                     input[1][1] * input[2][0]) /
                    determinant;
    inverse[2][1] = (input[0][1] * input[2][0] -
                     input[0][0] * input[2][1]) /
                    determinant;
    inverse[2][2] = (input[0][0] * input[1][1] -
                     input[0][1] * input[1][0]) /
                    determinant;

    for (const auto& row : inverse) {
        for (const auto value : row) {
            if (!std::isfinite(value) || std::fabs(value) > kMaximumInverseGain) {
                return false;
            }
        }
    }
    return true;
}

ChannelDecision classify_normalized(const float normalized,
                                    const float min_confidence) {
    if (!std::isfinite(normalized)) {
        return ChannelDecision{false, true, 0.0F};
    }
    const auto confidence = std::fabs((normalized * 2.0F) - 1.0F);
    return ChannelDecision{normalized >= 0.5F,
                           confidence < min_confidence,
                           confidence};
}

ChannelDecision classify_channel(const float observation,
                                 const float black,
                                 const float one,
                                 const float min_confidence) {
    if (!std::isfinite(observation)) {
        return ChannelDecision{false, true, 0.0F};
    }
    const auto normalized = (observation - black) / (one - black);
    return classify_normalized(normalized, min_confidence);
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

Status decode_rgb8_decisions(
    const std::span<const Rgb8Decision> decisions,
    const std::uint64_t expected_bytes,
    std::vector<std::byte>& payload,
    std::vector<std::uint32_t>& erasure_positions,
    const std::size_t max_payload_bytes) {
    if (expected_bytes > max_payload_bytes ||
        expected_bytes > kMaxRgb8PayloadBytes ||
        expected_bytes > (std::numeric_limits<std::size_t>::max() / 8U)) {
        return Status::resource_limit;
    }
    const auto cell_count = required_cells(expected_bytes);
    if (decisions.size() != cell_count) {
        return Status::invalid_argument;
    }

    try {
        std::vector<std::byte> candidate_payload(
            static_cast<std::size_t>(expected_bytes), std::byte{0});
        std::vector<bool> erased_bytes(
            static_cast<std::size_t>(expected_bytes), false);
        const auto total_bits = static_cast<std::size_t>(expected_bytes) * 8U;

        for (std::size_t cell = 0U; cell < decisions.size(); ++cell) {
            const auto& decision = decisions[cell];
            if (decision.symbol > 0x07U) {
                return Status::invalid_argument;
            }
            for (unsigned int bit = 0U; bit < 3U; ++bit) {
                const auto bit_index = cell * 3U + bit;
                if (bit_index >= total_bits) {
                    if (!decision.erasure &&
                        ((decision.symbol >> (2U - bit)) & 1U) != 0U) {
                        return Status::integrity;
                    }
                    continue;
                }

                const auto byte_index = bit_index / 8U;
                if (decision.erasure) {
                    erased_bytes[byte_index] = true;
                    continue;
                }

                const auto bit_value = static_cast<std::uint8_t>(
                    (decision.symbol >> (2U - bit)) & 1U);
                const auto bit_in_byte = 7U - (bit_index % 8U);
                auto byte = std::to_integer<std::uint8_t>(
                    candidate_payload[byte_index]);
                byte = static_cast<std::uint8_t>(
                    byte | (bit_value << bit_in_byte));
                candidate_payload[byte_index] = static_cast<std::byte>(byte);
            }
        }

        std::vector<std::uint32_t> candidate_erasures;
        for (std::size_t byte_index = 0U;
             byte_index < erased_bytes.size(); ++byte_index) {
            if (erased_bytes[byte_index]) {
                candidate_erasures.push_back(
                    static_cast<std::uint32_t>(byte_index));
            }
        }
        payload = std::move(candidate_payload);
        erasure_positions = std::move(candidate_erasures);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status fit_rgb8_calibration(const Rgb8Observation& black,
                            const Rgb8Observation& red,
                            const Rgb8Observation& green,
                            const Rgb8Observation& blue,
                            const float min_confidence,
                            Rgb8Calibration& calibration) {
    if (!finite_observation(black) || !finite_observation(red) ||
        !finite_observation(green) || !finite_observation(blue) ||
        !std::isfinite(min_confidence) || min_confidence < 0.0F ||
        min_confidence > 1.0F) {
        return Status::invalid_argument;
    }

    Matrix3 forward{};
    const std::array<Rgb8Observation, 3> patches{red, green, blue};
    for (std::size_t column = 0U; column < patches.size(); ++column) {
        forward[0][column] = patches[column].red - black.red;
        forward[1][column] = patches[column].green - black.green;
        forward[2][column] = patches[column].blue - black.blue;
    }

    Matrix3 inverse{};
    if (!invert_matrix(forward, inverse)) {
        return Status::invalid_argument;
    }

    Rgb8Calibration candidate;
    candidate.black = black;
    candidate.inverse_mixing = inverse;
    candidate.use_mixing_matrix = true;
    candidate.min_confidence = min_confidence;
    calibration = candidate;
    return Status::ok;
}

Status classify_rgb8_cell(const Rgb8Observation& observation,
                          const Rgb8Calibration& calibration,
                          Rgb8Decision& decision) {
    if (!finite_observation(calibration.black) ||
        !std::isfinite(calibration.min_confidence) ||
        calibration.min_confidence < 0.0F ||
        calibration.min_confidence > 1.0F) {
        return Status::invalid_argument;
    }

    ChannelDecision red;
    ChannelDecision green;
    ChannelDecision blue;
    if (calibration.use_mixing_matrix) {
        if (!finite_observation(observation)) {
            decision = Rgb8Decision{};
            return Status::ok;
        }
        for (const auto& row : calibration.inverse_mixing) {
            for (const auto value : row) {
                if (!std::isfinite(value) ||
                    std::fabs(value) > kMaximumInverseGain) {
                    return Status::invalid_argument;
                }
            }
        }
        std::array<float, 3> normalized{};
        for (std::size_t row = 0U; row < normalized.size(); ++row) {
            normalized[row] =
                calibration.inverse_mixing[row][0U] *
                    (observation.red - calibration.black.red) +
                calibration.inverse_mixing[row][1U] *
                    (observation.green - calibration.black.green) +
                calibration.inverse_mixing[row][2U] *
                    (observation.blue - calibration.black.blue);
            if (!std::isfinite(normalized[row])) {
                return Status::invalid_argument;
            }
        }
        red = classify_normalized(normalized[0U],
                                  calibration.min_confidence);
        green = classify_normalized(normalized[1U],
                                    calibration.min_confidence);
        blue = classify_normalized(normalized[2U],
                                   calibration.min_confidence);
    } else {
        if (!std::isfinite(calibration.one.red) ||
            !std::isfinite(calibration.one.green) ||
            !std::isfinite(calibration.one.blue) ||
            calibration.one.red <= calibration.black.red ||
            calibration.one.green <= calibration.black.green ||
            calibration.one.blue <= calibration.black.blue) {
            return Status::invalid_argument;
        }
        red = classify_channel(observation.red, calibration.black.red,
                               calibration.one.red,
                               calibration.min_confidence);
        green = classify_channel(observation.green, calibration.black.green,
                                 calibration.one.green,
                                 calibration.min_confidence);
        blue = classify_channel(observation.blue, calibration.black.blue,
                                calibration.one.blue,
                                calibration.min_confidence);
    }
    decision.symbol = static_cast<std::uint8_t>(
        (red.bit ? 0x04U : 0U) | (green.bit ? 0x02U : 0U) |
        (blue.bit ? 0x01U : 0U));
    decision.erasure = red.erasure || green.erasure || blue.erasure;
    decision.confidence = {red.confidence, green.confidence, blue.confidence};
    return Status::ok;
}

}  // namespace glyph
