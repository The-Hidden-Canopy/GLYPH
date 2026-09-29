#include "glyph/frame/scheduler.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    glyph::FrameScheduler scheduler;
    glyph::FrameScheduleConfig config;
    config.frame_hold = 3U;
    config.max_logical_frames = 4U;
    assert(glyph::FrameScheduler::create(config, scheduler) ==
           glyph::Status::ok);

    std::vector<glyph::FrameEmission> emissions;
    assert(scheduler.schedule(10U, 2U, emissions) == glyph::Status::ok);
    const std::vector<glyph::FrameEmission> expected{
        {10U, 0U}, {10U, 1U}, {10U, 2U},
        {11U, 0U}, {11U, 1U}, {11U, 2U},
    };
    assert(emissions == expected);

    const auto before_failure = emissions;
    assert(scheduler.schedule(0xffffffffU, 2U, emissions) ==
           glyph::Status::protocol);
    assert(emissions == before_failure);
    assert(scheduler.schedule(10U, 5U, emissions) ==
           glyph::Status::invalid_argument);
    assert(emissions == before_failure);

    glyph::FrameScheduler large_scheduler;
    glyph::FrameScheduleConfig large_config;
    large_config.frame_hold = static_cast<std::uint16_t>(
        glyph::kMaxFrameHold);
    large_config.max_logical_frames = glyph::kMaxScheduledFrames;
    assert(glyph::FrameScheduler::create(large_config, large_scheduler) ==
           glyph::Status::ok);
    assert(large_scheduler.schedule(0U, large_config.max_logical_frames,
                                    emissions) == glyph::Status::resource_limit);
    assert(emissions == before_failure);

    glyph::FrameScheduleConfig invalid_config = config;
    invalid_config.frame_hold = 0U;
    assert(glyph::FrameScheduler::create(invalid_config, scheduler) ==
           glyph::Status::invalid_argument);

    glyph::FrameSequenceWindow window;
    assert(glyph::FrameSequenceWindow::create({3U}, window) ==
           glyph::Status::ok);
    bool accepted = false;
    assert(window.observe(100U, accepted) == glyph::Status::ok);
    assert(accepted);
    assert(window.observe(100U, accepted) == glyph::Status::ok);
    assert(!accepted);
    assert(window.observe(99U, accepted) == glyph::Status::ok);
    assert(accepted);
    assert(window.observe(99U, accepted) == glyph::Status::ok);
    assert(!accepted);
    assert(window.observe(101U, accepted) == glyph::Status::ok);
    assert(accepted);
    assert(window.observe(102U, accepted) == glyph::Status::ok);
    assert(accepted);
    assert(window.observe(99U, accepted) == glyph::Status::ok);
    assert(!accepted);
    assert(window.observe(200U, accepted) == glyph::Status::ok);
    assert(accepted);
    assert(window.observe(100U, accepted) == glyph::Status::ok);
    assert(!accepted);

    window.reset();
    assert(!window.initialized());
    assert(window.observe(7U, accepted) == glyph::Status::ok);
    assert(accepted);

    assert(glyph::FrameSequenceWindow::create({0U}, window) ==
           glyph::Status::invalid_argument);
    return 0;
}
