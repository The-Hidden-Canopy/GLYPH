#include "glyph/frame/scheduler.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace glyph {

Status FrameScheduler::create(const FrameScheduleConfig& config,
                              FrameScheduler& scheduler) {
    if (config.frame_hold == 0U ||
        static_cast<std::uint32_t>(config.frame_hold) > kMaxFrameHold ||
        config.max_logical_frames == 0U ||
        config.max_logical_frames > kMaxScheduledFrames) {
        return Status::invalid_argument;
    }

    FrameScheduler candidate;
    candidate.frame_hold_ = config.frame_hold;
    candidate.max_logical_frames_ = config.max_logical_frames;
    scheduler = candidate;
    return Status::ok;
}

Status FrameScheduler::schedule(
    const std::uint32_t first_frame_seq,
    const std::uint32_t logical_frame_count,
    std::vector<FrameEmission>& emissions) const {
    if (frame_hold_ == 0U || max_logical_frames_ == 0U ||
        logical_frame_count == 0U ||
        logical_frame_count > max_logical_frames_) {
        return Status::invalid_argument;
    }
    if (logical_frame_count - 1U >
        std::numeric_limits<std::uint32_t>::max() - first_frame_seq) {
        return Status::protocol;
    }

    const auto physical_count =
        static_cast<std::uint64_t>(logical_frame_count) * frame_hold_;
    if (physical_count > kMaxScheduledEmissions ||
        physical_count > std::vector<FrameEmission>().max_size()) {
        return Status::resource_limit;
    }

    try {
        std::vector<FrameEmission> candidate;
        candidate.reserve(static_cast<std::size_t>(physical_count));
        for (std::uint32_t logical = 0U; logical < logical_frame_count;
             ++logical) {
            const auto frame_seq = first_frame_seq + logical;
            for (std::uint16_t hold = 0U; hold < frame_hold_; ++hold) {
                candidate.push_back(FrameEmission{frame_seq, hold});
            }
        }
        emissions = std::move(candidate);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status FrameSequenceWindow::create(const FrameSequenceWindowConfig& config,
                                   FrameSequenceWindow& window) {
    if (config.window_size == 0U ||
        config.window_size > kMaxSequenceWindow) {
        return Status::invalid_argument;
    }

    try {
        FrameSequenceWindow candidate;
        candidate.window_size_ = config.window_size;
        candidate.seen_.assign(config.window_size, false);
        window = std::move(candidate);
        return Status::ok;
    } catch (const std::bad_alloc&) {
        return Status::resource_limit;
    }
}

Status FrameSequenceWindow::observe(const std::uint32_t frame_seq,
                                    bool& accepted) {
    if (window_size_ == 0U || seen_.size() != window_size_) {
        return Status::invalid_argument;
    }
    accepted = false;

    if (!initialized_) {
        initialized_ = true;
        highest_frame_seq_ = frame_seq;
        seen_[0U] = true;
        accepted = true;
        return Status::ok;
    }

    if (frame_seq > highest_frame_seq_) {
        const auto advance = frame_seq - highest_frame_seq_;
        if (advance >= window_size_) {
            std::fill(seen_.begin(), seen_.end(), false);
        } else {
            for (std::size_t index = seen_.size(); index-- > 0U;) {
                const auto source = index >= advance
                                         ? index - advance
                                         : seen_.size();
                seen_[index] = source < seen_.size() ? seen_[source] : false;
            }
        }
        highest_frame_seq_ = frame_seq;
        seen_[0U] = true;
        accepted = true;
        return Status::ok;
    }

    const auto distance = highest_frame_seq_ - frame_seq;
    if (distance >= window_size_) {
        return Status::ok;
    }
    const auto index = static_cast<std::size_t>(distance);
    if (!seen_[index]) {
        seen_[index] = true;
        accepted = true;
    }
    return Status::ok;
}

void FrameSequenceWindow::reset() noexcept {
    std::fill(seen_.begin(), seen_.end(), false);
    highest_frame_seq_ = 0U;
    initialized_ = false;
}

}  // namespace glyph
