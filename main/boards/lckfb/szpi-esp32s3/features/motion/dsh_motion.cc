#include "dsh_motion.h"

#if !CONFIG_USE_EMOTE_MESSAGE_STYLE
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/task.h>

#include "application.h"
#include "mcp_server.h"

namespace {
constexpr char kTag[] = "DshMotion";
constexpr float kRad = dsh::kPi / 180;
constexpr float kAccelScale = 1.0f / 8192;  // CTRL2 +/-4g, 16-bit output.
constexpr float kGyroScale = kRad / 64;     // CTRL3 +/-512 degrees/s.
int16_t Decode(const uint8_t* p) {
    return static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}
}  // namespace

bool DshMotion::Read(uint8_t reg, uint8_t* data, size_t length) {
    return i2c_master_transmit_receive(device_, &reg, 1, data, length, 20) == ESP_OK;
}
bool DshMotion::Write(uint8_t reg, uint8_t value) {
    uint8_t data[] = {reg, value};
    return i2c_master_transmit(device_, data, sizeof(data), 20) == ESP_OK;
}

bool DshMotion::Initialize(i2c_master_bus_handle_t bus, DshPetDisplay* display) {
    if (device_)
        return true;
    // Official SZPI board schematic/tutorial: QMI8658 at 0x6A on GPIO1/2.
    // Reuse the board's existing master bus; do not install another I2C driver.
    if (!bus || !display || i2c_master_probe(bus, 0x6A, 50) != ESP_OK) {
        ESP_LOGW(kTag, "QMI8658 absent; motion interaction disabled");
        return false;
    }
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = 0x6A;
    config.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bus, &config, &device_) != ESP_OK)
        return false;
    uint8_t id = 0;
    bool ok = Read(0x00, &id, 1) && id == 0x05 && Write(0x08, 0x00) && Write(0x02, 0x40) &&
              Write(0x03, 0x15) && Write(0x04, 0x55) && Write(0x08, 0x03);
    // CTRL2/3 bit7 is self-test: leave it OFF. Both sensors use 250 Hz ODR;
    // read at 50 Hz, using measured elapsed time rather than assuming 20 ms.
    mutex_ = ok ? xSemaphoreCreateMutex() : nullptr;
    display_ = display;
    if (!mutex_ || xTaskCreate([](void* p) { static_cast<DshMotion*>(p)->Run(); }, "dsh_imu", 6144,
                               this, 2, nullptr) != pdPASS) {
        Write(0x08, 0x00);
        i2c_master_bus_rm_device(device_);
        device_ = nullptr;
        if (mutex_)
            vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
        ESP_LOGW(kTag, "QMI8658 initialization failed; continuing without motion");
        return false;
    }
    RegisterTools();
    ESP_LOGI(kTag, "QMI8658 ready: 50Hz quaternion error-state EKF; keep still for calibration");
    return true;
}

DshMotion::Snapshot DshMotion::GetSnapshot() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    auto copy = snapshot_;
    xSemaphoreGive(mutex_);
    return copy;
}

void DshMotion::PublishUi(dsh::Vec3 angles, dsh::Gesture gesture) {
    // At most ONE queued callback even if startup, rendering or audio is busy.
    // Preserve a gesture arriving while that callback is queued; use the newest
    // sample when it runs instead of displaying an old captured orientation.
    (void)angles;
    if (gesture != dsh::Gesture::None)
        pending_gesture_.store(gesture);
    if (ui_pending_.exchange(true))
        return;
    Application::GetInstance().Schedule([this]() {
        auto sample = GetSnapshot();
        auto gesture = pending_gesture_.exchange(dsh::Gesture::None);
        bool fresh = esp_timer_get_time() - sample.time_us < 300000;
        display_->UpdateMotion(sample.angles, gesture,
                               enabled_.load() && sample.calibrated && fresh);
        ui_pending_.store(false);
    });
}

void DshMotion::Run() {
    dsh::AttitudeFilter filter;
    dsh::GestureDetector gestures;
    dsh::Quaternion reference;
    dsh::Vec3 sum, previous_accel;
    int quiet_samples = 0;
    bool calibrated = false;
    int64_t previous_us = esp_timer_get_time(), last_ui = 0, last_log = 0;
    uint32_t samples = 0, errors = 0, consecutive_errors = 0;
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(20));
        int64_t now = esp_timer_get_time();
        if (calibrate_.exchange(false)) {
            filter.Reset();
            gestures = {};
            calibrated = false;
            quiet_samples = 0;
            sum = {};
            ESP_LOGI(kTag, "Calibration requested: keep the board still for 1.5 seconds");
        }
        uint8_t status = 0, raw[12] = {};
        if (!Read(0x2E, &status, 1) || (status & 3) != 3 || !Read(0x35, raw, sizeof(raw))) {
            ++errors;
            ++consecutive_errors;
            xSemaphoreTake(mutex_, portMAX_DELAY);
            snapshot_.errors = errors;
            if (now - snapshot_.time_us > 300000)
                snapshot_.calibrated = false;
            xSemaphoreGive(mutex_);
            // A stalled sensor must not leave a local gesture/tilt on screen.
            if (now - last_ui >= 100000) {
                PublishUi({}, dsh::Gesture::None);
                last_ui = now;
            }
            if (consecutive_errors >= 5) {
                // Back off a disconnected/stalled sensor instead of flooding
                // the shared audio I2C bus with failed transactions.
                vTaskDelay(pdMS_TO_TICKS(200));
                wake = xTaskGetTickCount();
            }
            continue;
        }
        consecutive_errors = 0;
        dsh::Vec3 accel{Decode(raw) * kAccelScale, Decode(raw + 2) * kAccelScale,
                        Decode(raw + 4) * kAccelScale};
        dsh::Vec3 gyro{Decode(raw + 6) * kGyroScale, Decode(raw + 8) * kGyroScale,
                       Decode(raw + 10) * kGyroScale};
        float dt = (now - previous_us) * 1e-6f;
        previous_us = now;
        if (dt > 0.1f) {
            // Missing samples cannot be reconstructed by integrating one stale
            // angular rate over a long gap. Re-align and recalibrate instead.
            filter.Reset();
            gestures = {};
            calibrated = false;
            quiet_samples = 0;
            sum = {};
            dt = 0.02f;
        }
        if (!filter.Update(accel, gyro, dt))
            continue;
        ++samples;
        if (!calibrated) {
            bool quiet = std::abs(accel.Norm() - 1) < 0.08f && gyro.Norm() < 0.15f &&
                         (accel - previous_accel).Norm() < 0.03f;
            if (quiet) {
                sum = sum + gyro;
                ++quiet_samples;
            } else {
                quiet_samples = 0;
                sum = {};
            }
            if (quiet_samples >= 75) {
                filter.SetBias(sum * (1.0f / quiet_samples));
                reference = filter.orientation();
                calibrated = true;
                ESP_LOGI(kTag, "Stationary calibration complete: bias=[%.4f,%.4f,%.4f] rad/s",
                         filter.bias().x, filter.bias().y, filter.bias().z);
            }
        }
        previous_accel = accel;
        auto q = filter.orientation();
        auto relative = reference.Conjugate() * q;
        auto angles = relative.EulerDegrees();
        float tilt = std::acos(std::clamp(relative.Gravity().z, -1.0f, 1.0f)) * 180 / dsh::kPi;
        float linear = (accel - q.Gravity()).Norm();
        dsh::Gesture gesture = dsh::Gesture::None;
        // Ignore gestures during speech, setup, calibration and disabled mode.
        if (calibrated && enabled_.load() &&
            Application::GetInstance().GetDeviceState() == kDeviceStateIdle)
            gesture = gestures.Update(angles, tilt, linear, (gyro - filter.bias()).Norm(), dt);
        else
            gestures = {};
        xSemaphoreTake(mutex_, portMAX_DELAY);
        snapshot_ = {q, angles, filter.bias(), linear, samples, errors, now, calibrated};
        xSemaphoreGive(mutex_);
        if (gesture != dsh::Gesture::None || dsh_animation_ui_due(last_ui, now)) {
            PublishUi(angles, gesture);
            last_ui = now;
        }
        if (now - last_log >= 10000000) {
            ESP_LOGI(kTag,
                     "Attitude: roll=%.1f pitch=%.1f yaw=%.1f calibrated=%d samples=%lu errors=%lu",
                     angles.x, angles.y, angles.z, calibrated, static_cast<unsigned long>(samples),
                     static_cast<unsigned long>(errors));
            last_log = now;
        }
    }
}

void DshMotion::RegisterTools() {
    auto& server = McpServer::GetInstance();
    server.AddTool(
        "self.pet.motion.status",
        "Read DSH pet QMI8658 orientation, relative tilt, gyro bias, calibration and sample "
        "freshness. "
        "Yaw is relative and may drift: this six-axis sensor has no compass.",
        PropertyList(), [this](const PropertyList&) -> ToolResult {
            auto s = GetSnapshot();
            CJsonUniquePtr json(cJSON_CreateObject());
            if (!json)
                return std::unexpected("Could not allocate motion status");
            cJSON_AddBoolToObject(json.get(), "enabled", enabled_.load());
            cJSON_AddBoolToObject(json.get(), "calibrated", s.calibrated);
            cJSON_AddNumberToObject(json.get(), "sample_age_ms",
                                    s.time_us ? (esp_timer_get_time() - s.time_us) / 1000 : -1);
            cJSON_AddNumberToObject(json.get(), "roll_deg", s.angles.x);
            cJSON_AddNumberToObject(json.get(), "pitch_deg", s.angles.y);
            cJSON_AddNumberToObject(json.get(), "yaw_relative_deg", s.angles.z);
            double q[] = {s.q.w, s.q.x, s.q.y, s.q.z};
            cJSON_AddItemToObject(json.get(), "quaternion_wxyz", cJSON_CreateDoubleArray(q, 4));
            double b[] = {s.bias.x, s.bias.y, s.bias.z};
            cJSON_AddItemToObject(json.get(), "gyro_bias_rad_s", cJSON_CreateDoubleArray(b, 3));
            cJSON_AddNumberToObject(json.get(), "linear_acceleration_g", s.linear_g);
            cJSON_AddNumberToObject(json.get(), "samples", s.samples);
            cJSON_AddNumberToObject(json.get(), "read_errors", s.errors);
            return json.release();  // McpTool takes ownership of returned cJSON*.
        });
    server.AddTool("self.pet.motion.enable", "Enable or disable local tilt/shake pet interactions.",
                   PropertyList({Property("enabled", kPropertyTypeBoolean)}),
                   [this](const PropertyList& p) -> ToolResult {
                       enabled_.store(p["enabled"].value<bool>());
                       return true;
                   });
    server.AddTool("self.pet.motion.calibrate",
                   "Recalibrate the gyro bias and adopt the current resting pose as neutral. "
                   "Keep the device stationary for 1.5 seconds; completion is asynchronous, check "
                   "motion.status.",
                   PropertyList(), [this](const PropertyList&) -> ToolResult {
                       calibrate_.store(true);
                       return true;
                   });
}
#endif
