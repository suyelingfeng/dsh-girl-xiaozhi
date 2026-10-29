#include "dsh_pet_display.h"
#if !CONFIG_USE_EMOTE_MESSAGE_STYLE
#include <esp_log.h>
#include <esp_timer.h>
#include <cstring>
#include "application.h"
#include "display/lvgl_display/lvgl_theme.h"
namespace {
constexpr char kTag[] = "DshPetDisplay";
const char* Emotion(dsh::Gesture gesture) {
    switch (gesture) {
        case dsh::Gesture::TiltLeft:
            return "embarrassed";
        case dsh::Gesture::TiltRight:
            return "winking";
        case dsh::Gesture::Shake:
            return "surprised";
        case dsh::Gesture::Flip:
            return "angry";
        case dsh::Gesture::Sleep:
            return "sleepy";
        case dsh::Gesture::Wake:
            return "happy";
        default:
            return nullptr;
    }
}
}  // namespace
bool DshPetDisplay::SetEmotion(const char* emotion) {
    ++emotion_generation_;
    neutral_.store(emotion &&
                   (std::strcmp(emotion, "neutral") == 0 || std::strcmp(emotion, "relaxed") == 0 ||
                    std::strcmp(emotion, "robot_2") == 0));
    return ShowEmotion(emotion);
}

bool DshPetDisplay::ShowEmotion(const char* emotion) {
    if (!emotion)
        return false;
    DisplayLockGuard lock(this);
    if (!lock)
        return false;
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    auto collection = theme ? theme->emoji_collection() : nullptr;
    auto* image = collection ? collection->GetEmojiImage(emotion) : nullptr;
    auto* source = image ? image->image_dsc() : nullptr;
    // Repeated cloud status updates must not restart a ten-second motion.
    // Include source identity: boot-time asset/theme replacement can keep the
    // emotion name while supplying a completely different animation.
    if (emotion && shown_emotion_ == emotion && source == shown_source_ &&
        (animation_.IsPlaying() || (gif_controller_ && gif_controller_->IsPlaying())))
        return true;
    if (source && source->data_size >= 4 && std::memcmp(source->data, "DSA1", 4) == 0) {
        if (gif_controller_) {
            gif_controller_->Stop();
            gif_controller_.reset();
        }
        if (!animation_.Start(source, emoji_image_))
            return false;
        shown_emotion_ = emotion;
        shown_source_ = source;
        if (emoji_label_)
            lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        if (display_)
            lv_timer_set_period(lv_display_get_refr_timer(display_), 10);
        return true;
    }
    animation_.Stop();
    if (!SpiLcdDisplay::SetEmotion(emotion))
        return false;
    shown_emotion_ = emotion ? emotion : "";
    shown_source_ = source;
    if (emoji_image_ && gif_controller_ && gif_controller_->IsLoaded()) {
        // Decode fewer pixels, retain the original on-screen character size.
        lv_image_set_scale(emoji_image_, 176 * 256 / gif_controller_->width());
        // The descriptor and buffer stay fixed. Invalidation avoids repeating
        // source setup/layout and cache work on every decoded GIF frame.
        gif_controller_->SetFrameCallback([this]() { lv_obj_invalidate(emoji_image_); });
    } else if (emoji_image_) {
        lv_image_set_scale(emoji_image_, 256);
    }
    if (display_)
        lv_timer_set_period(lv_display_get_refr_timer(display_), 20);
    return true;
}

void DshPetDisplay::ResetMotion() {
    visual_ = {};
    DisplayLockGuard lock(this);
    if (!lock || !emoji_image_)
        return;
    lv_image_set_rotation(emoji_image_, 0);
    lv_obj_set_style_translate_x(emoji_image_, 0, 0);
    lv_obj_set_style_translate_y(emoji_image_, 0, 0);
}

void DshPetDisplay::UpdateMotion(dsh::Vec3 angles, dsh::Gesture gesture, bool enabled) {
    // Invoked only by Application::Schedule, never from the sampling task.
    int64_t now = esp_timer_get_time();
    bool idle = Application::GetInstance().GetDeviceState() == kDeviceStateIdle;
    if (local_active_ && local_generation_ != emotion_generation_.load()) {
        local_active_ = false;
        sleeping_ = false;
    }
    if (local_active_ && (!enabled || !idle || (!sleeping_ && now >= local_until_))) {
        // A newer cloud emotion invalidated local_active_ above.
        local_active_ = false;
        sleeping_ = false;
        SetEmotion("neutral");
    }
    if (!enabled || !idle || (!neutral_.load() && !local_active_)) {
        ResetMotion();
        return;
    }
    if (const char* emotion = Emotion(gesture)) {
        if (ShowEmotion(emotion)) {
            local_active_ = true;
            local_generation_ = emotion_generation_.load();
            local_until_ = now + 6000000;
            sleeping_ = gesture == dsh::Gesture::Sleep;
            ESP_LOGI(kTag, "Pet interaction: %s", emotion);
        }
    }
    DisplayLockGuard lock(this);
    if (!lock || !emoji_image_)
        return;
    // Smooth sensor noise using elapsed time, independent of callback jitter.
    visual_ = dsh_animation_step(visual_, angles.x, angles.y, now);
    // Small visual response follows relative tilt; never rotate the whole UI.
    // A quiet sensor must take LVGL's untransformed fast path, rather than
    // rotating the whole sprite for fractions of a degree of measurement noise.
    int angle = std::abs(visual_.roll) < 2
                    ? 0
                    : static_cast<int>(std::clamp(visual_.roll * 2, -80.0f, 80.0f));
    if (angle < 0)
        angle += 3600;
    lv_image_set_rotation(emoji_image_, angle);
    lv_obj_set_style_translate_x(emoji_image_,
                                 static_cast<int>(std::clamp(visual_.roll / 4, -10.0f, 10.0f)), 0);
    lv_obj_set_style_translate_y(emoji_image_,
                                 static_cast<int>(std::clamp(visual_.pitch / 8, -5.0f, 5.0f)), 0);
}

#endif
