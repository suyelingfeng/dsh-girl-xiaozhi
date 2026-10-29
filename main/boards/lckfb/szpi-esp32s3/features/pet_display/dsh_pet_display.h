#pragma once
#include "sdkconfig.h"
#if !CONFIG_USE_EMOTE_MESSAGE_STYLE
#include <atomic>
#include <string>
#include "../animation/dsh_animation_player.h"
#include "../gestures/gesture_detector.h"
#include "../motion/pet_attitude.h"
#include "display/lcd_display.h"
// Only this board's display gains motion interaction. Ordinary cloud emotions
// invalidate the local animation, so a delayed idle restore cannot erase them.
class DshPetDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;
    bool SetEmotion(const char* emotion) override;
    void UpdateMotion(dsh::Vec3 angles, dsh::Gesture gesture, bool enabled);

private:
    std::atomic<uint32_t> emotion_generation_{0};
    std::atomic<bool> neutral_{true};
    uint32_t local_generation_ = 0;
    int64_t local_until_ = 0;
    bool local_active_ = false;
    bool sleeping_ = false;
    std::string shown_emotion_;
    const lv_image_dsc_t* shown_source_ = nullptr;
    DshAnimationState visual_;
    DshAnimationPlayer animation_;
    bool ShowEmotion(const char* emotion);
    void ResetMotion();
};

#endif
