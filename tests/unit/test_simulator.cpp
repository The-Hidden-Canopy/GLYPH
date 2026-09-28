#include "glyph/sim/simulator.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

glyph::optical::OpticalSurface make_surface() {
    glyph::optical::OpticalSurface surface;
    surface.width = 9U;
    surface.height = 4U;
    surface.frame_seq = 8U;
    surface.hold_index = 2U;
    surface.pixels.resize(surface.width * surface.height);
    for (std::size_t index = 0U; index < surface.pixels.size(); ++index) {
        surface.pixels[index] = glyph::optical::Rgba8{
            static_cast<std::uint8_t>((index * 13U) & 0xffU),
            static_cast<std::uint8_t>((index * 29U + 3U) & 0xffU),
            static_cast<std::uint8_t>((index * 47U + 7U) & 0xffU), 255U};
    }
    return surface;
}

}  // namespace

int main() {
    using glyph::Status;
    const auto source = make_surface();

    glyph::sim::ImpairmentConfig identity_config;
    glyph::optical::OpticalSurface captured;
    glyph::sim::CaptureReceipt receipt;
    assert(glyph::sim::simulate_capture(source, identity_config, captured,
                                        receipt) == Status::ok);
    assert(captured.pixels == source.pixels);
    assert(captured.frame_seq == source.frame_seq);
    assert(captured.hold_index == source.hold_index);
    assert(receipt.seed == identity_config.seed);
    assert(receipt.pixels_processed == source.pixels.size());
    assert(receipt.pixels_dropped == 0U);
    assert(receipt.channels_noised == 0U);

    glyph::sim::ImpairmentConfig config;
    config.seed = 0x123456789abcdef0ULL;
    config.noise_amplitude = 11U;
    config.dropout_percent = 23U;
    config.quantization_bits = 5U;
    glyph::optical::OpticalSurface first;
    glyph::sim::CaptureReceipt first_receipt;
    assert(glyph::sim::simulate_capture(source, config, first, first_receipt) ==
           Status::ok);
    glyph::optical::OpticalSurface second;
    glyph::sim::CaptureReceipt second_receipt;
    assert(glyph::sim::simulate_capture(source, config, second,
                                        second_receipt) == Status::ok);
    assert(first.pixels == second.pixels);
    assert(first_receipt == second_receipt);
    assert(first_receipt.pixels_dropped > 0U);
    assert(first_receipt.channels_noised > 0U);

    auto changed_seed = config;
    changed_seed.seed++;
    glyph::optical::OpticalSurface different;
    glyph::sim::CaptureReceipt different_receipt;
    assert(glyph::sim::simulate_capture(source, changed_seed, different,
                                        different_receipt) == Status::ok);
    assert(different.pixels != first.pixels ||
           different_receipt != first_receipt);

    auto all_drop = config;
    all_drop.noise_amplitude = 0U;
    all_drop.dropout_percent = 100U;
    const auto before_failure = captured;
    const auto receipt_before_failure = receipt;
    assert(glyph::sim::simulate_capture(source, all_drop, captured, receipt) ==
           Status::ok);
    assert(receipt.pixels_dropped == source.pixels.size());
    const glyph::optical::Rgba8 black{0U, 0U, 0U, 255U};
    for (const auto pixel : captured.pixels) {
        assert(pixel == black);
    }

    auto bad_config = config;
    bad_config.quantization_bits = 0U;
    assert(glyph::sim::simulate_capture(source, bad_config, captured, receipt) ==
           Status::invalid_argument);
    assert(captured.pixels ==
           std::vector<glyph::optical::Rgba8>(source.pixels.size(),
                                               glyph::optical::Rgba8{0U, 0U,
                                                                     0U, 255U}));
    static_cast<void>(before_failure);
    static_cast<void>(receipt_before_failure);

    auto malformed_source = source;
    malformed_source.pixels.pop_back();
    assert(glyph::sim::simulate_capture(malformed_source, config, captured,
                                        receipt) == Status::invalid_argument);

    return 0;
}
