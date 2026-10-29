#pragma once
#include "sdkconfig.h"
#if !CONFIG_USE_EMOTE_MESSAGE_STYLE
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <atomic>
#include "dsh_animation_wake.h"
#include "rust/animation.h"

// Platform adapter only: buffers, ROM inflate and LVGL calls. Rust validates the
// format, reconstructs pixels and computes deadlines. Public control methods
// require the display lock. The worker never takes it; only its short buffer
// handoff uses a separate critical section shared with the GUI callback.
class DshAnimationPlayer {
public:
    DshAnimationPlayer() = default;
    ~DshAnimationPlayer();
    DshAnimationPlayer(const DshAnimationPlayer&) = delete;
    DshAnimationPlayer& operator=(const DshAnimationPlayer&) = delete;
    bool Start(const lv_image_dsc_t* source, lv_obj_t* image);
    void Stop();
    bool IsPlaying() const { return timer_ != nullptr; }

private:
    const lv_image_dsc_t* source_ = nullptr;
    lv_image_dsc_t source_descriptor_ = {};
    lv_obj_t* image_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    lv_image_dsc_t descriptors_[3] = {};
    DshAnimationInfo info_ = {};
    uint8_t *indices_ = nullptr, *delta_ = nullptr, *pixels_[3] = {};
    void* inflater_ = nullptr;
    uint32_t frame_ = 0, timeline_frame_ = 0, drawn_ = 0, presented_ = 0;
    int64_t origin_us_ = 0, stats_us_ = 0, decode_max_us_ = 0;
    int64_t inflate_max_us_ = 0, render_max_us_ = 0;
    portMUX_TYPE slots_lock_ = portMUX_INITIALIZER_UNLOCKED;
    int front_ = 0, ready_[2] = {}, ready_count_ = 0;
    TaskHandle_t worker_ = nullptr;
    SemaphoreHandle_t worker_done_ = nullptr;
    std::atomic<bool> stopping_{false}, failed_{false};
    bool Decode(int slot);
    void Run();
    void Tick();
};
#endif
