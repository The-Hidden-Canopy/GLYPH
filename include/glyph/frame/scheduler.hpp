#pragma once

#include "glyph/core/status.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace glyph {

inline constexpr std::uint16_t kDefaultFrameHold = 1U;
inline constexpr std::uint32_t kMaxFrameHold = 1024U;
inline constexpr std::uint32_t kMaxScheduledFrames = 1U << 20U;
inline constexpr std::uint64_t kMaxScheduledEmissions = 16ULL << 20U;
inline constexpr std::uint32_t kMaxSequenceWindow = 1U << 20U;

struct FrameScheduleConfig {
    std::uint16_t frame_hold = kDefaultFrameHold;
    std::uint32_t max_logical_frames = kMaxScheduledFrames;
};

struct FrameEmission {
    std::uint32_t frame_seq = 0U;
    std::uint16_t hold_index = 0U;

    friend constexpr bool operator==(const FrameEmission&,
                                     const FrameEmission&) = default;
};

class FrameScheduler final {
public:
    FrameScheduler() = default;

    [[nodiscard]] static Status create(const FrameScheduleConfig& config,
                                       FrameScheduler& scheduler);

    // Expands logical sequence numbers into physical presentation emissions.
    // Every hold repeats the same logical frame_seq; hold_index is local to
    // that presentation and is not part of the wire frame sequence.
    [[nodiscard]] Status schedule(
        std::uint32_t first_frame_seq,
        std::uint32_t logical_frame_count,
        std::vector<FrameEmission>& emissions) const;

    [[nodiscard]] std::uint16_t frame_hold() const noexcept {
        return frame_hold_;
    }

    [[nodiscard]] std::uint32_t max_logical_frames() const noexcept {
        return max_logical_frames_;
    }

private:
    std::uint16_t frame_hold_ = 0U;
    std::uint32_t max_logical_frames_ = 0U;
};

struct FrameSequenceWindowConfig {
    std::uint32_t window_size = 1024U;
};

class FrameSequenceWindow final {
public:
    FrameSequenceWindow() = default;

    [[nodiscard]] static Status create(
        const FrameSequenceWindowConfig& config,
        FrameSequenceWindow& window);

    // Returns accepted=true only once for a sequence in the bounded window.
    // Older or repeated frames are safely suppressed with Status::ok.
    [[nodiscard]] Status observe(std::uint32_t frame_seq, bool& accepted);

    void reset() noexcept;

    [[nodiscard]] std::uint32_t window_size() const noexcept {
        return window_size_;
    }

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }

    [[nodiscard]] std::uint32_t highest_frame_seq() const noexcept {
        return highest_frame_seq_;
    }

private:
    std::uint32_t window_size_ = 0U;
    std::uint32_t highest_frame_seq_ = 0U;
    std::vector<bool> seen_;
    bool initialized_ = false;
};

}  // namespace glyph
