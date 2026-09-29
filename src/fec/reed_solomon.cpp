#include "glyph/fec/reed_solomon.hpp"

#include "gf256.hpp"

#include <algorithm>
#include <cstddef>
#include <new>
#include <utility>

namespace glyph {
namespace {

constexpr std::uint32_t kMaxShards = 255U;
constexpr std::uint32_t kMaxShardBytes = 16U << 20U;
constexpr std::uint64_t kMaxWorkspaceBytes = 256ULL << 20U;

using Matrix = fec_detail::Matrix;
using fec_detail::invert_matrix;
using fec_detail::mul;
using fec_detail::power;

Matrix make_vandermonde(const std::size_t rows,
                        const std::size_t columns) {
    Matrix matrix(
        rows, std::vector<std::uint8_t>(columns, 0U));
    for (std::size_t row = 0U; row < rows; ++row) {
        for (std::size_t column = 0U; column < columns; ++column) {
            matrix[row][column] =
                power(static_cast<std::uint8_t>(row), column);
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
            output[column] ^= mul(row[inner], matrix[inner][column]);
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
        const auto product = mul(
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

    try {
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
            multiply_row_by_matrix(vandermonde[row], top_inverse,
                                   generator[row]);
        }

        SystematicFec candidate;
        candidate.data_shards_ = config.data_shards;
        candidate.parity_shards_ = config.parity_shards;
        candidate.shard_bytes_ = config.shard_bytes;
        candidate.generator_ = std::move(generator);
        fec = std::move(candidate);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
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

    try {
        for (std::size_t parity = 0U; parity < parity_shards_; ++parity) {
            auto& output = shards[data_shards_ + parity];
            output.assign(shard_bytes_, std::byte{0});
            const auto& coefficients = generator_[data_shards_ + parity];
            for (std::size_t data = 0U; data < data_shards_; ++data) {
                add_scaled_shard(output, shards[data], coefficients[data]);
            }
        }
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
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

    try {
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
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

}  // namespace glyph
