#include "glyph/sim/simulator.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>

namespace glyph::sim {
namespace {

constexpr std::uint8_t kMaximumNoiseAmplitude = 64U;

class SplitMix64 {
  public:
    explicit SplitMix64(const std::uint64_t seed) : state_(seed) {}

    std::uint64_t next() noexcept {
        state_ += 0x9e3779b97f4a7c15ULL;
        auto value = state_;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

  private:
    std::uint64_t state_;
};

std::uint8_t add_noise(const std::uint8_t value,
                       const std::uint8_t amplitude,
                       SplitMix64& random) noexcept {
    if (amplitude == 0U) {
        return value;
    }
    const auto range = static_cast<std::uint32_t>(amplitude) * 2U + 1U;
    const auto offset = static_cast<int>(random.next() % range) - amplitude;
    const auto noisy = static_cast<int>(value) + offset;
    return static_cast<std::uint8_t>(noisy < 0 ? 0 : noisy > 255 ? 255 : noisy);
}

std::uint8_t quantize(const std::uint8_t value,
                      const std::uint8_t bits) noexcept {
    if (bits >= 8U) {
        return value;
    }
    const auto levels = (1U << bits) - 1U;
    const auto level = (static_cast<std::uint32_t>(value) * levels + 127U) /
                       255U;
    return static_cast<std::uint8_t>(
        (level * 255U + levels / 2U) / levels);
}

}  // namespace

Status simulate_capture(const optical::OpticalSurface& source,
                        const ImpairmentConfig& config,
                        optical::OpticalSurface& captured,
                        CaptureReceipt& receipt) {
    if (source.width == 0U || source.height == 0U ||
        source.width > std::numeric_limits<std::size_t>::max() /
                           source.height ||
        source.pixels.size() != static_cast<std::size_t>(source.width) *
                                     source.height ||
        config.noise_amplitude > kMaximumNoiseAmplitude ||
        config.dropout_percent > 100U || config.quantization_bits == 0U ||
        config.quantization_bits > 8U) {
        return Status::invalid_argument;
    }

    try {
        optical::OpticalSurface candidate;
        candidate.width = source.width;
        candidate.height = source.height;
        candidate.frame_seq = source.frame_seq;
        candidate.hold_index = source.hold_index;
        candidate.pixels.resize(source.pixels.size());

        SplitMix64 random(config.seed);
        CaptureReceipt candidate_receipt;
        candidate_receipt.seed = config.seed;
        candidate_receipt.pixels_processed = source.pixels.size();
        for (std::size_t index = 0U; index < source.pixels.size(); ++index) {
            const auto& input = source.pixels[index];
            if (config.dropout_percent != 0U &&
                random.next() % 100U < config.dropout_percent) {
                candidate.pixels[index] = optical::Rgba8{0U, 0U, 0U, 255U};
                ++candidate_receipt.pixels_dropped;
                continue;
            }
            auto red = add_noise(input.red, config.noise_amplitude, random);
            auto green = add_noise(input.green, config.noise_amplitude, random);
            auto blue = add_noise(input.blue, config.noise_amplitude, random);
            if (config.noise_amplitude != 0U) {
                candidate_receipt.channels_noised += 3U;
            }
            red = quantize(red, config.quantization_bits);
            green = quantize(green, config.quantization_bits);
            blue = quantize(blue, config.quantization_bits);
            candidate.pixels[index] = optical::Rgba8{red, green, blue,
                                                     input.alpha};
        }

        captured = std::move(candidate);
        receipt = candidate_receipt;
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

}  // namespace glyph::sim
