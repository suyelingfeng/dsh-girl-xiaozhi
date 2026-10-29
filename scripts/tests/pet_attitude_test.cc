#include "main/boards/lckfb/szpi-esp32s3/features/motion/pet_attitude.h"
#include <cassert>
#include <cmath>
#include <limits>
#include "main/boards/lckfb/szpi-esp32s3/features/gestures/gesture_detector.h"

using namespace dsh;
static void Unit(const Quaternion& q) {
    assert(std::abs(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z - 1) < 1e-4f);
}
int main() {
    AttitudeFilter filter;
    assert(!filter.Update({0, 0, 0}, {}, 0.01f));
    assert(!filter.Update({0, 0, 1}, {}, std::numeric_limits<float>::quiet_NaN()));
    assert(!filter.Update({0, 0, 1}, {}, 1));
    assert(filter.Update({0, 0, 1}, {}, 0.01f));
    auto original = filter.orientation();
    assert(!filter.Update({std::numeric_limits<float>::infinity(), 0, 1}, {}, 0.01f));
    assert(filter.orientation().w == original.w);

    // Gravity correction must converge roll/pitch and observable gyro bias,
    // while preserving a normalized quaternion and nonnegative covariance.
    for (int i = 0; i < 6000; ++i) {
        float noise = 0.004f * std::sin(i * 0.73f);
        assert(filter.Update({noise, -noise, 1}, {0.025f, -0.02f, 0}, 0.01f));
        Unit(filter.orientation());
        for (int j = 0; j < 6; ++j)
            assert(std::isfinite(filter.Variance(j)) && filter.Variance(j) >= 0);
    }
    assert((filter.orientation().Gravity() - Vec3{0, 0, 1}).Norm() < 0.015f);
    assert(std::abs(filter.bias().x - 0.025f) < 0.005f);
    assert(std::abs(filter.bias().y + 0.02f) < 0.005f);

    // Smooth 90-degree roll, followed by a hold: acceleration must be fused
    // in the same body frame/sign as the quaternion gyro prediction.
    filter.Reset();
    filter.Update({0, 0, 1}, {}, 0.01f);
    for (int i = 1; i <= 200; ++i) {
        auto truth = Quaternion::Rotation({kPi * 0.5f * i / 200, 0, 0});
        assert(filter.Update(truth.Gravity(), {kPi / 4, 0, 0}, 0.01f));
    }
    for (int i = 0; i < 500; ++i)
        filter.Update({0, 1, 0}, {}, 0.01f);
    assert(std::abs(filter.orientation().EulerDegrees().x - 90) < 1);
    Unit(filter.orientation());

    // Pure yaw is relative dead reckoning; gravity must not suppress it.
    filter.Reset();
    filter.Update({0, 0, 1}, {}, 0.01f);
    for (int i = 0; i < 200; ++i)
        filter.Update({0, 0, 1}, {0, 0, kPi / 4}, 0.01f);
    assert(std::abs(filter.orientation().EulerDegrees().z - 90) < 1);

    // Noncommuting three-axis motion, including high pitch. Comparing the
    // quaternion (up to sign) avoids Euler wrap/singularity artifacts.
    filter.Reset();
    filter.Update({0, 0, 1}, {}, 0.01f);
    Quaternion truth;
    for (int i = 0; i < 1600; ++i) {
        Vec3 rate{0.6f * std::sin(i * 0.003f), 0.5f * std::cos(i * 0.004f), 0.2f};
        truth = truth * Quaternion::Rotation(rate * 0.01f);
        truth.Normalize();
        filter.Update(truth.Gravity(), rate, 0.01f);
        const auto& q = filter.orientation();
        float dot = std::abs(q.w * truth.w + q.x * truth.x + q.y * truth.y + q.z * truth.z);
        assert(dot > 0.999f);
        Unit(q);
    }

    // Large translational acceleration must not drag a stationary attitude.
    filter.Reset();
    filter.Update({0, 0, 1}, {}, 0.01f);
    for (int i = 0; i < 100; ++i)
        filter.Update({2, 0, 1}, {}, 0.01f);
    assert(filter.orientation().Gravity().z > 0.999f);
    // Initialization must also handle gravity directly opposite world +Z.
    filter.Reset();
    assert(filter.Update({0, 0, -1}, {}, 0.01f));
    assert(filter.orientation().Gravity().z < -0.999f);
    Unit(filter.orientation());

    GestureDetector detector;
    int tilts = 0;
    for (int i = 0; i < 500; ++i)
        tilts += detector.Update({35, 0, 0}, 35, 0, 0, 0.02f) == Gesture::TiltRight;
    assert(tilts == 1);  // Held tilt is latched, not repeated every cooldown.
    detector.Update({}, 0, 0, 0, 0.02f);
    for (int i = 0; i < 30; ++i)
        tilts += detector.Update({35, 0, 0}, 35, 0, 0, 0.02f) == Gesture::TiltRight;
    assert(tilts == 2);

    detector = {};
    int flips = 0;
    for (int i = 0; i < 400; ++i)
        flips += detector.Update({180, 0, 0}, 180, 0, 0, 0.02f) == Gesture::Flip;
    assert(flips == 1);
    detector = {};
    int shakes = 0;
    for (int i = 0; i < 3; ++i) {
        shakes += detector.Update({}, 0, 1, 0, 0.1f) == Gesture::Shake;
        shakes += detector.Update({}, 0, 0, 0, 0.1f) == Gesture::Shake;
    }
    assert(shakes == 1);
    // One impact, even held high, is not a repeated shake.
    detector = {};
    for (int i = 0; i < 200; ++i)
        assert(detector.Update({}, 0, 1, 0, 0.02f) != Gesture::Shake);
    detector = {};
    int sleeps = 0;
    for (int i = 0; i < 2400; ++i)
        sleeps += detector.Update({}, 0, 0, 0, 0.02f) == Gesture::Sleep;
    assert(sleeps == 1);
    assert(detector.Update({}, 0, 0.3f, 0, 0.02f) == Gesture::Wake);
}
