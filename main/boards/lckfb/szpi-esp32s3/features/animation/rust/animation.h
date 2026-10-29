#pragma once

#include <cstddef>
#include <cstdint>

// Keep in sync with Rust #[repr(C)] MotionState; values cross the ABI by value.
struct DshAnimationState {
    float roll = 0;
    float pitch = 0;
    int64_t time_us = 0;
};
static_assert(sizeof(DshAnimationState) == 16);
extern "C" bool dsh_animation_ui_due(int64_t previous_us, int64_t now_us);
extern "C" DshAnimationState dsh_animation_step(DshAnimationState state, float roll, float pitch,
                                                int64_t now_us);

struct DshAnimationInfo {
    uint16_t width, height;
    uint32_t frames;
};
struct DshAnimationFrame {
    const uint8_t* data;
    size_t size;
};
extern "C" bool dsh_animation_info(const uint8_t* data, size_t size, DshAnimationInfo* out);
extern "C" bool dsh_animation_frame(const uint8_t* data, size_t size, uint32_t index,
                                    DshAnimationFrame* out);
extern "C" bool dsh_animation_apply(const uint8_t* data, size_t size, uint8_t* previous,
                                    const uint8_t* delta, uint8_t* pixels, size_t count);
extern "C" uint32_t dsh_animation_delay_ms(int64_t origin, uint32_t frame, int64_t now);
