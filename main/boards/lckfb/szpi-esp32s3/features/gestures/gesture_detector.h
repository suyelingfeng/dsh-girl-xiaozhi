#pragma once
#include "../motion/pet_attitude.h"

// Debounced gestures consume fused motion; they never access hardware or UI.
namespace dsh {
enum class Gesture { None, TiltLeft, TiltRight, Shake, Flip, Sleep, Wake };
class GestureDetector {
public:
    Gesture Update(Vec3 relative_angles, float tilt, float linear_g, float rate, float dt) {
        clock_ += dt;
        bool moving = linear_g > 0.12f || rate > 0.12f;
        if (moving)
            still_ = 0;
        else
            still_ += dt;
        if (sleeping_ && moving) {
            sleeping_ = false;
            return Gesture::Wake;
        }
        if (!sleeping_ && still_ >= 45) {
            sleeping_ = true;
            return Gesture::Sleep;
        }
        if (linear_g > 0.7f && !shake_high_) {
            if (clock_ - last_pulse_ > 0.8f)
                pulses_ = 0;
            ++pulses_;
            last_pulse_ = clock_;
        }
        shake_high_ = linear_g > 0.35f;
        bool flipped = tilt > 135;
        bool tilted = std::abs(relative_angles.x) > 28 || std::abs(relative_angles.y) > 28;
        if (tilt < 100)
            flip_latched_ = false;
        if (std::abs(relative_angles.x) < 15 && std::abs(relative_angles.y) < 15)
            tilt_latched_ = false;
        flip_hold_ = flipped ? flip_hold_ + dt : 0;
        tilt_hold_ = tilted ? tilt_hold_ + dt : 0;
        if (clock_ < cooldown_)
            return Gesture::None;
        Gesture event = Gesture::None;
        if (pulses_ >= 3 && clock_ - last_pulse_ < 0.8f) {
            event = Gesture::Shake;
            pulses_ = 0;
        } else if (flip_hold_ > 0.35f && !flip_latched_) {
            event = Gesture::Flip;
            flip_latched_ = true;
        } else if (tilt_hold_ > 0.3f && !tilt_latched_ && !flipped) {
            event = relative_angles.x < 0 ? Gesture::TiltLeft : Gesture::TiltRight;
            tilt_latched_ = true;
        }
        if (event != Gesture::None)
            cooldown_ = clock_ + 3;
        return event;
    }

private:
    float clock_ = 0, still_ = 0, cooldown_ = 0, last_pulse_ = -10, tilt_hold_ = 0, flip_hold_ = 0;
    int pulses_ = 0;
    bool sleeping_ = false, shake_high_ = false, tilt_latched_ = false, flip_latched_ = false;
};
}  // namespace dsh
