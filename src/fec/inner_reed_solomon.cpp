#include "glyph/fec/inner_reed_solomon.hpp"

#include "gf256.hpp"

#include <algorithm>
#include <cstddef>
#include <new>
#include <utility>

namespace glyph {
namespace {

constexpr std::size_t kMaxCodewordBytes = 255U;

std::vector<std::uint8_t> make_generator_polynomial(
    const std::size_t parity_bytes) {
    std::vector<std::uint8_t> generator{1U};
    for (std::size_t root_index = 0U; root_index < parity_bytes;
         ++root_index) {
        const auto root = fec_detail::power(2U, root_index);
        std::vector<std::uint8_t> next(generator.size() + 1U, 0U);
        for (std::size_t index = 0U; index < generator.size(); ++index) {
            next[index] ^= generator[index];
            next[index + 1U] ^=
                fec_detail::mul(generator[index], root);
        }
        generator = std::move(next);
    }
    return generator;
}

void encode_with_generator(std::span<const std::byte> data,
                           const std::vector<std::uint8_t>& generator,
                           std::vector<std::byte>& codeword) {
    const auto data_bytes = data.size();
    const auto parity_bytes = generator.size() - 1U;
    std::vector<std::uint8_t> work(data_bytes + parity_bytes, 0U);
    for (std::size_t index = 0U; index < data_bytes; ++index) {
        work[index] = std::to_integer<std::uint8_t>(data[index]);
    }

    for (std::size_t index = 0U; index < data_bytes; ++index) {
        const auto coefficient = work[index];
        if (coefficient == 0U) {
            continue;
        }
        for (std::size_t generator_index = 0U;
             generator_index < generator.size(); ++generator_index) {
            work[index + generator_index] ^=
                fec_detail::mul(generator[generator_index], coefficient);
        }
    }

    codeword.assign(data_bytes + parity_bytes, std::byte{0});
    for (std::size_t index = 0U; index < data_bytes; ++index) {
        codeword[index] = data[index];
    }
    for (std::size_t index = 0U; index < parity_bytes; ++index) {
        codeword[data_bytes + index] =
            static_cast<std::byte>(work[data_bytes + index]);
    }
}

std::vector<std::uint8_t> calculate_syndromes(
    const std::vector<std::byte>& codeword,
    const std::size_t parity_bytes) {
    std::vector<std::uint8_t> syndromes(parity_bytes, 0U);
    for (std::size_t syndrome_index = 0U; syndrome_index < parity_bytes;
         ++syndrome_index) {
        const auto evaluation_point = fec_detail::power(2U, syndrome_index);
        std::uint8_t value = 0U;
        for (const auto byte : codeword) {
            value = static_cast<std::uint8_t>(
                fec_detail::mul(value, evaluation_point) ^
                std::to_integer<std::uint8_t>(byte));
        }
        syndromes[syndrome_index] = value;
    }
    return syndromes;
}

bool all_zero(const std::vector<std::uint8_t>& values) {
    return std::all_of(values.begin(), values.end(), [](const auto value) {
        return value == 0U;
    });
}

std::vector<std::uint8_t> berlekamp_massey(
    const std::vector<std::uint8_t>& syndromes) {
    std::vector<std::uint8_t> locator{1U};
    std::vector<std::uint8_t> previous{1U};
    std::size_t degree = 0U;
    std::size_t shift = 1U;
    std::uint8_t previous_discrepancy = 1U;

    for (std::size_t index = 0U; index < syndromes.size(); ++index) {
        auto discrepancy = syndromes[index];
        for (std::size_t term = 1U; term <= degree; ++term) {
            discrepancy ^= fec_detail::mul(
                locator[term], syndromes[index - term]);
        }

        if (discrepancy == 0U) {
            ++shift;
            continue;
        }

        const auto previous_locator = locator;
        const auto scale = fec_detail::mul(
            discrepancy, fec_detail::inverse(previous_discrepancy));
        if (locator.size() < previous.size() + shift) {
            locator.resize(previous.size() + shift, 0U);
        }
        for (std::size_t term = 0U; term < previous.size(); ++term) {
            locator[term + shift] ^=
                fec_detail::mul(scale, previous[term]);
        }

        if (2U * degree <= index) {
            degree = index + 1U - degree;
            previous = previous_locator;
            previous_discrepancy = discrepancy;
            shift = 1U;
        } else {
            ++shift;
        }
    }

    locator.resize(degree + 1U);
    return locator;
}

std::vector<std::size_t> locate_errors(
    const std::vector<std::uint8_t>& locator,
    const std::size_t codeword_bytes) {
    const auto degree = locator.size() - 1U;
    std::vector<std::size_t> positions;
    for (std::size_t position = 0U; position < codeword_bytes; ++position) {
        const auto codeword_power = codeword_bytes - 1U - position;
        const auto inverse_point = fec_detail::power(
            2U, (255U - (codeword_power % 255U)) % 255U);
        std::uint8_t value = 0U;
        for (std::size_t term = degree + 1U; term-- > 0U;) {
            value = static_cast<std::uint8_t>(
                fec_detail::mul(value, inverse_point) ^ locator[term]);
        }
        if (value == 0U) {
            positions.push_back(position);
        }
    }
    return positions;
}

Status correct_error_magnitudes(std::vector<std::byte>& codeword,
                                const std::vector<std::uint8_t>& syndromes,
                                const std::vector<std::size_t>& positions) {
    const auto count = positions.size();
    if (count == 0U || count > syndromes.size()) {
        return Status::integrity;
    }

    fec_detail::Matrix equations(
        count, std::vector<std::uint8_t>(count, 0U));
    for (std::size_t row = 0U; row < count; ++row) {
        for (std::size_t column = 0U; column < count; ++column) {
            const auto point = fec_detail::power(
                2U, (codeword.size() - 1U - positions[column]) % 255U);
            equations[row][column] = fec_detail::power(point, row);
        }
    }

    fec_detail::Matrix inverse;
    if (fec_detail::invert_matrix(equations, inverse) != Status::ok) {
        return Status::integrity;
    }
    std::vector<std::uint8_t> magnitudes(count, 0U);
    for (std::size_t row = 0U; row < count; ++row) {
        for (std::size_t column = 0U; column < count; ++column) {
            magnitudes[row] ^= fec_detail::mul(
                inverse[row][column], syndromes[column]);
        }
    }
    for (std::size_t index = 0U; index < count; ++index) {
        const auto corrected = static_cast<std::uint8_t>(
            std::to_integer<std::uint8_t>(codeword[positions[index]]) ^
            magnitudes[index]);
        codeword[positions[index]] = static_cast<std::byte>(corrected);
    }
    return Status::ok;
}

}  // namespace

Status InnerReedSolomon::create(const InnerRsConfig& config,
                                InnerReedSolomon& code) {
    const auto codeword_bytes = static_cast<std::size_t>(config.data_bytes) +
                                config.parity_bytes;
    if (config.data_bytes == 0U || config.parity_bytes == 0U ||
        codeword_bytes > kMaxCodewordBytes || config.parity_bytes < 2U) {
        return Status::invalid_argument;
    }

    try {
        InnerReedSolomon candidate;
        candidate.data_bytes_ = config.data_bytes;
        candidate.parity_bytes_ = config.parity_bytes;
        candidate.generator_polynomial_ =
            make_generator_polynomial(config.parity_bytes);
        candidate.generator_matrix_.assign(
            codeword_bytes,
            std::vector<std::uint8_t>(config.data_bytes, 0U));

        for (std::size_t data_index = 0U; data_index < config.data_bytes;
             ++data_index) {
            std::vector<std::byte> unit(config.data_bytes, std::byte{0});
            unit[data_index] = std::byte{1};
            std::vector<std::byte> encoded;
            encode_with_generator(unit, candidate.generator_polynomial_, encoded);
            for (std::size_t position = 0U; position < codeword_bytes;
                 ++position) {
                candidate.generator_matrix_[position][data_index] =
                    std::to_integer<std::uint8_t>(encoded[position]);
            }
        }

        code = std::move(candidate);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status InnerReedSolomon::encode(const std::span<const std::byte> data,
                                std::vector<std::byte>& codeword) const {
    if (data_bytes_ == 0U || data.size() != data_bytes_) {
        return Status::invalid_argument;
    }
    try {
        std::vector<std::byte> candidate;
        encode_with_generator(data, generator_polynomial_, candidate);
        codeword = std::move(candidate);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status InnerReedSolomon::decode(std::vector<std::byte>& codeword) const {
    if (data_bytes_ == 0U || codeword.size() != codeword_bytes()) {
        return Status::invalid_argument;
    }
    try {
        const auto syndromes = calculate_syndromes(codeword, parity_bytes_);
        if (all_zero(syndromes)) {
            return Status::ok;
        }

        const auto locator = berlekamp_massey(syndromes);
        const auto degree = locator.size() - 1U;
        if (degree == 0U || (2U * degree) > parity_bytes_) {
            return Status::integrity;
        }
        const auto positions = locate_errors(locator, codeword.size());
        if (positions.size() != degree) {
            return Status::integrity;
        }
        auto corrected = codeword;
        if (correct_error_magnitudes(corrected, syndromes, positions) !=
            Status::ok) {
            return Status::integrity;
        }
        if (!all_zero(calculate_syndromes(corrected, parity_bytes_))) {
            return Status::integrity;
        }
        codeword = std::move(corrected);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status InnerReedSolomon::recover_erasures(
    std::vector<std::byte>& codeword,
    const std::span<const std::uint16_t> erasure_positions) const {
    if (data_bytes_ == 0U || codeword.size() != codeword_bytes()) {
        return Status::invalid_argument;
    }
    if (erasure_positions.size() > parity_bytes_) {
        return Status::integrity;
    }

    try {
        std::vector<bool> erased(codeword.size(), false);
        for (const auto position : erasure_positions) {
            if (position >= codeword.size() || erased[position]) {
                return Status::invalid_argument;
            }
            erased[position] = true;
        }
        if (erasure_positions.empty()) {
            return Status::ok;
        }

        std::vector<std::size_t> selected;
        selected.reserve(data_bytes_);
        for (std::size_t position = 0U;
             position < codeword.size() && selected.size() < data_bytes_;
             ++position) {
            if (!erased[position]) {
                selected.push_back(position);
            }
        }
        if (selected.size() != data_bytes_) {
            return Status::integrity;
        }

        fec_detail::Matrix equations(
            data_bytes_, std::vector<std::uint8_t>(data_bytes_, 0U));
        for (std::size_t row = 0U; row < data_bytes_; ++row) {
            equations[row] = generator_matrix_[selected[row]];
        }
        fec_detail::Matrix inverse;
        if (fec_detail::invert_matrix(equations, inverse) != Status::ok) {
            return Status::integrity;
        }

        std::vector<std::uint8_t> data(data_bytes_, 0U);
        for (std::size_t row = 0U; row < data_bytes_; ++row) {
            for (std::size_t column = 0U; column < data_bytes_; ++column) {
                data[row] ^= fec_detail::mul(
                    inverse[row][column],
                    std::to_integer<std::uint8_t>(codeword[selected[column]]));
            }
        }

        std::vector<std::byte> reconstructed(codeword.size(), std::byte{0});
        for (std::size_t position = 0U; position < reconstructed.size();
             ++position) {
            for (std::size_t data_index = 0U; data_index < data.size();
                 ++data_index) {
                reconstructed[position] = static_cast<std::byte>(
                    std::to_integer<std::uint8_t>(reconstructed[position]) ^
                    fec_detail::mul(generator_matrix_[position][data_index],
                                    data[data_index]));
            }
            if (!erased[position] &&
                reconstructed[position] != codeword[position]) {
                return Status::integrity;
            }
        }
        codeword = std::move(reconstructed);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

}  // namespace glyph
