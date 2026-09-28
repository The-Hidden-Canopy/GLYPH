#include "glyph/fec/reed_solomon.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <utility>

namespace glyph {
namespace {

constexpr std::uint32_t kMaxShards = 255U;
constexpr std::uint32_t kMaxShardBytes = 16U << 20U;
constexpr std::uint64_t kMaxWorkspaceBytes = 256ULL << 20U;

using Matrix = std::vector<std::vector<std::uint8_t>>;

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

const GaloisTables& tables() {
    static const GaloisTables value;
    return value;
}

std::uint8_t gf_mul(const std::uint8_t left, const std::uint8_t right) {
    if (left == 0U || right == 0U) {
        return 0U;
    }
    const auto& lookup = tables();
    return lookup.exponent[lookup.logarithm[left] + lookup.logarithm[right]];
}

std::uint8_t gf_inverse(const std::uint8_t value) {
    if (value == 0U) {
        return 0U;
    }
    const auto& lookup = tables();
    return lookup.exponent[255U - lookup.logarithm[value]];
}

std::uint8_t gf_pow(const std::uint8_t base, const std::size_t exponent) {
    if (exponent == 0U) {
        return 1U;
    }
    if (base == 0U) {
        return 0U;
    }
    const auto& lookup = tables();
    const auto power = (static_cast<std::size_t>(lookup.logarithm[base]) *
                        exponent) %
                       255U;
    return lookup.exponent[power];
}

Status invert_matrix(const Matrix& input, Matrix& inverse) {
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

        const auto pivot_inverse = gf_inverse(augmented[column][column]);
        for (std::size_t entry = 0U; entry < size * 2U; ++entry) {
            augmented[column][entry] =
                gf_mul(augmented[column][entry], pivot_inverse);
        }

        for (std::size_t row = 0U; row < size; ++row) {
            if (row == column || augmented[row][column] == 0U) {
                continue;
            }
            const auto factor = augmented[row][column];
            for (std::size_t entry = 0U; entry < size * 2U; ++entry) {
                augmented[row][entry] ^=
                    gf_mul(factor, augmented[column][entry]);
            }
        }
    }

    inverse.assign(size, std::vector<std::uint8_t>(size, 0U));
    for (std::size_t row = 0U; row < size; ++row) {
        std::copy(augmented[row].begin() + static_cast<std::ptrdiff_t>(size),
                  augmented[row].end(), inverse[row].begin());
    }
    return Status::ok;
}

Matrix make_vandermonde(const std::size_t rows,
                        const std::size_t columns) {
    Matrix matrix(
        rows, std::vector<std::uint8_t>(columns, 0U));
    for (std::size_t row = 0U; row < rows; ++row) {
        for (std::size_t column = 0U; column < columns; ++column) {
            matrix[row][column] =
                gf_pow(static_cast<std::uint8_t>(row), column);
        }
    }
    return matrix;
}

void multiply_row_by_matrix(const std::vector<std::uint8_t>& row,
                            const Matrix& matrix,
                            std::vector<std::uint8_t>& output) {
    std::fill(output.begin(), output.end(), 0U);
    for (std::size_t inner = 0U; inner < row.size(); ++inner) {
        if (row[inner] == 0U) {
            continue;
        }
        for (std::size_t column = 0U; column < output.size(); ++column) {
            output[column] ^= gf_mul(row[inner], matrix[inner][column]);
        }
    }
}

void add_scaled_shard(std::vector<std::byte>& destination,
                      const std::vector<std::byte>& source,
                      const std::uint8_t coefficient) {
    if (coefficient == 0U) {
        return;
    }
    for (std::size_t index = 0U; index < destination.size(); ++index) {
        const auto product = gf_mul(
            coefficient, std::to_integer<std::uint8_t>(source[index]));
        destination[index] ^= static_cast<std::byte>(product);
    }
}

}  // namespace

Status SystematicFec::create(const FecConfig& config, SystematicFec& fec) {
    if (config.data_shards == 0U || config.parity_shards == 0U ||
        config.shard_bytes == 0U) {
        return Status::invalid_argument;
    }
    const auto total_shards = static_cast<std::uint32_t>(config.data_shards) +
                              static_cast<std::uint32_t>(config.parity_shards);
    if (total_shards > kMaxShards) {
        return Status::resource_limit;
    }
    if (config.shard_bytes > kMaxShardBytes) {
        return Status::resource_limit;
    }
    if (static_cast<std::uint64_t>(config.data_shards) *
            config.shard_bytes >
        kMaxWorkspaceBytes) {
        return Status::resource_limit;
    }

    const auto vandermonde = make_vandermonde(total_shards,
                                              config.data_shards);
    Matrix top(vandermonde.begin(),
               vandermonde.begin() + config.data_shards);
    Matrix top_inverse;
    if (invert_matrix(top, top_inverse) != Status::ok) {
        return Status::integrity;
    }

    Matrix generator(total_shards,
                     std::vector<std::uint8_t>(config.data_shards, 0U));
    for (std::size_t row = 0U; row < total_shards; ++row) {
        multiply_row_by_matrix(vandermonde[row], top_inverse, generator[row]);
    }

    fec.data_shards_ = config.data_shards;
    fec.parity_shards_ = config.parity_shards;
    fec.shard_bytes_ = config.shard_bytes;
    fec.generator_ = std::move(generator);
    return Status::ok;
}

Status SystematicFec::encode(
    std::vector<std::vector<std::byte>>& shards) const {
    if (data_shards_ == 0U || parity_shards_ == 0U ||
        shards.size() != static_cast<std::size_t>(data_shards_ + parity_shards_)) {
        return Status::invalid_argument;
    }
    for (std::size_t index = 0U; index < data_shards_; ++index) {
        if (shards[index].size() != shard_bytes_) {
            return Status::invalid_argument;
        }
    }

    for (std::size_t parity = 0U; parity < parity_shards_; ++parity) {
        auto& output = shards[data_shards_ + parity];
        output.assign(shard_bytes_, std::byte{0});
        const auto& coefficients = generator_[data_shards_ + parity];
        for (std::size_t data = 0U; data < data_shards_; ++data) {
            add_scaled_shard(output, shards[data], coefficients[data]);
        }
    }
    return Status::ok;
}

Status SystematicFec::reconstruct(
    std::vector<std::vector<std::byte>>& shards,
    std::vector<bool>& present) const {
    const auto total_shards = static_cast<std::size_t>(data_shards_ + parity_shards_);
    if (data_shards_ == 0U || parity_shards_ == 0U ||
        shards.size() != total_shards || present.size() != total_shards) {
        return Status::invalid_argument;
    }

    std::size_t available = 0U;
    for (std::size_t index = 0U; index < total_shards; ++index) {
        if (present[index]) {
            if (shards[index].size() != shard_bytes_) {
                return Status::invalid_argument;
            }
            ++available;
        }
    }
    if (available < data_shards_) {
        return Status::integrity;
    }

    std::vector<std::size_t> selected;
    selected.reserve(data_shards_);
    for (std::size_t index = 0U;
         index < total_shards && selected.size() < data_shards_; ++index) {
        if (present[index]) {
            selected.push_back(index);
        }
    }

    Matrix submatrix(data_shards_,
                     std::vector<std::uint8_t>(data_shards_, 0U));
    for (std::size_t row = 0U; row < data_shards_; ++row) {
        submatrix[row] = generator_[selected[row]];
    }
    Matrix inverse;
    if (invert_matrix(submatrix, inverse) != Status::ok) {
        return Status::integrity;
    }

    std::vector<std::vector<std::byte>> recovered_data(
        data_shards_, std::vector<std::byte>(shard_bytes_, std::byte{0}));
    for (std::size_t data = 0U; data < data_shards_; ++data) {
        for (std::size_t row = 0U; row < data_shards_; ++row) {
            add_scaled_shard(recovered_data[data], shards[selected[row]],
                             inverse[data][row]);
        }
    }

    for (std::size_t data = 0U; data < data_shards_; ++data) {
        if (!present[data]) {
            shards[data] = std::move(recovered_data[data]);
            present[data] = true;
        }
    }

    for (std::size_t parity = 0U; parity < parity_shards_; ++parity) {
        const auto index = data_shards_ + parity;
        if (present[index]) {
            continue;
        }
        shards[index].assign(shard_bytes_, std::byte{0});
        const auto& coefficients = generator_[index];
        for (std::size_t data = 0U; data < data_shards_; ++data) {
            add_scaled_shard(shards[index], shards[data], coefficients[data]);
        }
        present[index] = true;
    }
    return Status::ok;
}

}  // namespace glyph
