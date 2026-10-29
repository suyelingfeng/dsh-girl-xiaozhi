#pragma once

#include "sdkconfig.h"

#if !CONFIG_USE_EMOTE_MESSAGE_STYLE
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>

#include "../gestures/gesture_detector.h"
#include "../pet_display/dsh_pet_display.h"
#include "pet_attitude.h"

class DshMotion {
public:
    // Missing sensor or task allocation failure disables only motion features.
    bool Initialize(i2c_master_bus_handle_t bus, DshPetDisplay* display);

private:
    struct Snapshot {
        dsh::Quaternion q;
        dsh::Vec3 angles, bias;
        float linear_g = 0;
        uint32_t samples = 0, errors = 0;
        int64_t time_us = 0;
        bool calibrated = false;
    };
    i2c_master_dev_handle_t device_ = nullptr;
    DshPetDisplay* display_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    Snapshot snapshot_;
    std::atomic<bool> enabled_{true};
    std::atomic<bool> calibrate_{false};
    std::atomic<bool> ui_pending_{false};
    std::atomic<dsh::Gesture> pending_gesture_{dsh::Gesture::None};
    bool Read(uint8_t reg, uint8_t* data, size_t length);
    bool Write(uint8_t reg, uint8_t value);
    void Run();
    void RegisterTools();
    Snapshot GetSnapshot();
    void PublishUi(dsh::Vec3 angles, dsh::Gesture gesture);
};
#endif
