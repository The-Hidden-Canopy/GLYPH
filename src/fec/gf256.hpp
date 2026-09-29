#pragma once

#include "glyph/core/status.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace glyph::fec_detail {

struct GaloisTables {
    std::array<std::uint8_t, 512> exponent{};
    std::array<std::uint8_t, 256> logarithm{};

    GaloisTables() {
        std::uint16_t value = 1U;
        for (std::uint16_t index = 0U; index < 255U; ++index) {
            exponent[index] = static_cast<std::uint8_t>(value);
            logarithm[value] = static_cast<std::uint8_t>(index);
            value = static_cast<std::uint16_t>(value << 1U);
            if ((value & 0x100U) != 0U) {
                value = static_cast<std::uint16_t>(value ^ 0x11dU);
            }
        }
        for (std::size_t index = 255U; index < exponent.size(); ++index) {
            exponent[index] = exponent[index - 255U];
        }
    }
};

inline const GaloisTables& tables() {
    static const GaloisTables value;
    return value;
}

inline std::uint8_t mul(const std::uint8_t left,
                        const std::uint8_t right) {
    if (left == 0U || right == 0U) {
        return 0U;
    }
    const auto& lookup = tables();
    return lookup.exponent[lookup.logarithm[left] + lookup.logarithm[right]];
}

inline std::uint8_t inverse(const std::uint8_t value) {
    if (value == 0U) {
        return 0U;
    }
    const auto& lookup = tables();
    return lookup.exponent[255U - lookup.logarithm[value]];
}

inline std::uint8_t power(const std::uint8_t base,
                          const std::size_t exponent) {
    if (exponent == 0U) {
        return 1U;
    }
    if (base == 0U) {
        return 0U;
    }
    const auto& lookup = tables();
    const auto power_index =
        (static_cast<std::size_t>(lookup.logarithm[base]) * exponent) % 255U;
    return lookup.exponent[power_index];
}

using Matrix = std::vector<std::vector<std::uint8_t>>;

inline Status invert_matrix(const Matrix& input, Matrix& inverse_matrix) {
    const auto size = input.size();
    if (size == 0U) {
        return Status::invalid_argument;
    }
    for (const auto& row : input) {
        if (row.size() != size) {
            return Status::invalid_argument;
        }
    }

    Matrix augmented(size, std::vector<std::uint8_t>(size * 2U, 0U));
    for (std::size_t row = 0U; row < size; ++row) {
        for (std::size_t column = 0U; column < size; ++column) {
            augmented[row][column] = input[row][column];
        }
        augmented[row][size + row] = 1U;
    }

    for (std::size_t column = 0U; column < size; ++column) {
        std::size_t pivot = column;
        while (pivot < size && augmented[pivot][column] == 0U) {
            ++pivot;
        }
        if (pivot == size) {
            return Status::integrity;
        }
        if (pivot != column) {
            std::swap(augmented[pivot], augmented[column]);
        }

        const auto pivot_inverse = inverse(augmented[column][column]);
        for (std::size_t entry = 0U; entry < size * 2U; ++entry) {
            augmented[column][entry] =
                mul(augmented[column][entry], pivot_inverse);
        }

        for (std::size_t row = 0U; row < size; ++row) {
            if (row == column || augmented[row][column] == 0U) {
                continue;
            }
            const auto factor = augmented[row][column];
            for (std::size_t entry = 0U; entry < size * 2U; ++entry) {
                augmented[row][entry] ^=
                    mul(factor, augmented[column][entry]);
            }
        }
    }

    inverse_matrix.assign(size, std::vector<std::uint8_t>(size, 0U));
    for (std::size_t row = 0U; row < size; ++row) {
        std::copy(augmented[row].begin() + static_cast<std::ptrdiff_t>(size),
                  augmented[row].end(), inverse_matrix[row].begin());
    }
    return Status::ok;
}

}  // namespace glyph::fec_detail
