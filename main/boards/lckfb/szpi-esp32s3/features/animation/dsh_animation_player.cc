#include "dsh_animation_player.h"
#if !CONFIG_USE_EMOTE_MESSAGE_STYLE
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <miniz.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {
constexpr char kTag[] = "DshAnimation";
constexpr unsigned kEdge = 176;
constexpr size_t kPixelBytes = kEdge * kEdge * 3;  // RGB565 plane + A8 plane.
}  // namespace

DshAnimationPlayer::~DshAnimationPlayer() { Stop(); }

void DshAnimationPlayer::Stop() {
    stopping_.store(true);
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    // The decoder never acquires the display lock. Joining it here is bounded
    // by one frame and one RTOS tick; release no borrowed asset/buffer early.
    if (worker_) {
        if (xSemaphoreTake(worker_done_, pdMS_TO_TICKS(250)) != pdTRUE) {
            ESP_LOGE(kTag, "Decoder did not stop; refusing to free active buffers");
            std::abort();
        }
        worker_ = nullptr;
    }
    if (image_) {
        for (auto& descriptor : descriptors_)
            if (lv_image_get_src(image_) == &descriptor)
                lv_image_set_src(image_, nullptr);
    }
    heap_caps_free(indices_);
    heap_caps_free(delta_);
    heap_caps_free(inflater_);
    for (auto& pixels : pixels_) {
        heap_caps_free(pixels);
        pixels = nullptr;
    }
    if (worker_done_) {
        vSemaphoreDelete(worker_done_);
        worker_done_ = nullptr;
    }
    indices_ = delta_ = nullptr;
    inflater_ = nullptr;
    source_ = nullptr;
    image_ = nullptr;
}

bool DshAnimationPlayer::Start(const lv_image_dsc_t* source, lv_obj_t* image) {
    Stop();
    if (!source || !image || !dsh_animation_info(source->data, source->data_size, &info_))
        return false;
    size_t count = size_t(info_.width) * info_.height;
    // All allocations occur on emotion changes. The hot Huffman state uses
    // SRAM; large index, delta and three presentation buffers use PSRAM.
    indices_ =
        static_cast<uint8_t*>(heap_caps_calloc(count, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    delta_ = static_cast<uint8_t*>(heap_caps_malloc(count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    inflater_ = heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    worker_done_ = xSemaphoreCreateBinary();
    for (auto& pixels : pixels_)
        pixels = static_cast<uint8_t*>(
            heap_caps_malloc(kPixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!indices_ || !delta_ || !inflater_ || !worker_done_ || !pixels_[0] || !pixels_[1] ||
        !pixels_[2]) {
        Stop();
        return false;
    }
    // Theme collections may release their descriptor object during a switch.
    // Keep our own descriptor while borrowing the stable mapped asset bytes.
    source_descriptor_ = *source;
    source_ = &source_descriptor_;
    image_ = image;
    for (int slot = 0; slot < 3; ++slot) {
        auto& descriptor = descriptors_[slot];
        descriptor = {};
        descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
        descriptor.header.cf = LV_COLOR_FORMAT_RGB565A8;
        descriptor.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
        descriptor.header.w = descriptor.header.h = kEdge;
        descriptor.header.stride = kEdge * 2;
        descriptor.data = pixels_[slot];
        descriptor.data_size = kPixelBytes;
    }
    frame_ = timeline_frame_ = drawn_ = presented_ = 0;
    front_ = 0;
    ready_count_ = 0;
    decode_max_us_ = inflate_max_us_ = render_max_us_ = 0;
    stopping_.store(false);
    failed_.store(false);
    origin_us_ = stats_us_ = esp_timer_get_time();
    if (!Decode(0)) {
        Stop();
        return false;
    }
    lv_image_set_src(image_, &descriptors_[0]);
    lv_image_set_scale(image_, 256);
    // The GUI callback only presents an already decoded buffer. Expensive Rust
    // pixel reconstruction and ROM inflate run independently on the other core.
    timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            static_cast<DshAnimationPlayer*>(lv_timer_get_user_data(timer))->Tick();
        },
        5, this);
    if (!timer_ ||
        xTaskCreatePinnedToCore([](void* self) { static_cast<DshAnimationPlayer*>(self)->Run(); },
                                "dsh_anim", 7168, this, 1, &worker_, 0) != pdPASS) {
        Stop();
        return false;
    }
    ESP_LOGI(kTag, "Rust DSA1 ready: %ux%u, 30Hz, %lu frames, triple buffer", info_.width,
             info_.height, static_cast<unsigned long>(info_.frames));
    return true;
}

bool DshAnimationPlayer::Decode(int slot) {
    int64_t begin = esp_timer_get_time();
    if (frame_ == info_.frames) {
        frame_ = 0;
        std::memset(indices_, 0, size_t(info_.width) * info_.height);
    }
    DshAnimationFrame compressed{};
    if (!dsh_animation_frame(source_->data, source_->data_size, frame_, &compressed))
        return false;
    auto* inflater = static_cast<tinfl_decompressor*>(inflater_);
    tinfl_init(inflater);
    size_t consumed = compressed.size, produced = size_t(info_.width) * info_.height;
    auto status =
        tinfl_decompress(inflater, compressed.data, &consumed, delta_, delta_, &produced,
                         TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    int64_t inflated = esp_timer_get_time();
    if (status != TINFL_STATUS_DONE || consumed != compressed.size ||
        produced != size_t(info_.width) * info_.height ||
        !dsh_animation_apply(source_->data, source_->data_size, indices_, delta_, pixels_[slot],
                             produced))
        return false;
    ++frame_;
    ++timeline_frame_;
    portENTER_CRITICAL(&slots_lock_);
    ++drawn_;
    decode_max_us_ = std::max(decode_max_us_, esp_timer_get_time() - begin);
    inflate_max_us_ = std::max(inflate_max_us_, inflated - begin);
    render_max_us_ = std::max(render_max_us_, esp_timer_get_time() - inflated);
    portEXIT_CRITICAL(&slots_lock_);
    return true;
}

void DshAnimationPlayer::Run() {
    while (!stopping_.load()) {
        uint32_t delay = dsh_animation_delay_ms(origin_us_, timeline_frame_, esp_timer_get_time());
        if (delay) {
            vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(delay)));
            continue;
        }
        // Keep at most two complete frames pending. Audio can preempt decode
        // and cause a short burst; a single "latest" slot would lose frames
        // even when the display has enough throughput to present all of them.
        portENTER_CRITICAL(&slots_lock_);
        int slot = -1;
        for (int candidate = 0; candidate < 3; ++candidate) {
            bool occupied = candidate == front_;
            for (int i = 0; i < ready_count_; ++i)
                occupied |= candidate == ready_[i];
            if (!occupied) {
                slot = candidate;
                break;
            }
        }
        portEXIT_CRITICAL(&slots_lock_);
        if (slot < 0) {
            // Backpressure is bounded by the three owned buffers. No extra
            // allocations, frame queues or writes into a displayed buffer.
            vTaskDelay(1);
            continue;
        }
        if (!Decode(slot)) {
            failed_.store(true);
            break;
        }
        portENTER_CRITICAL(&slots_lock_);
        ready_[ready_count_++] = slot;
        portEXIT_CRITICAL(&slots_lock_);
        DshAnimationNotifyFrame();
    }
    xSemaphoreGive(worker_done_);
    vTaskDelete(nullptr);
}

void DshAnimationPlayer::Tick() {
    if (failed_.load()) {
        ESP_LOGE(kTag, "Invalid DSA1 frame; stopping animation");
        Stop();
        return;
    }
    portENTER_CRITICAL(&slots_lock_);
    int slot = ready_count_ ? ready_[0] : -1;
    if (slot >= 0) {
        front_ = slot;
        --ready_count_;
        ready_[0] = ready_[1];
    }
    portEXIT_CRITICAL(&slots_lock_);
    if (slot >= 0) {
        lv_image_set_src(image_, &descriptors_[slot]);
        ++presented_;
    }
    int64_t now = esp_timer_get_time();
    if (now - stats_us_ >= 10000000) {
        portENTER_CRITICAL(&slots_lock_);
        auto decoded = drawn_;
        auto total = decode_max_us_;
        auto inflate = inflate_max_us_;
        auto render = render_max_us_;
        drawn_ = 0;
        decode_max_us_ = inflate_max_us_ = render_max_us_ = 0;
        portEXIT_CRITICAL(&slots_lock_);
        double scale = 1000000.0 / (now - stats_us_);
        ESP_LOGI(kTag,
                 "decoded_fps=%.1f presented_fps=%.1f max_decode_us=%lld inflate_max_us=%lld "
                 "rust_max_us=%lld",
                 decoded * scale, presented_ * scale, total, inflate, render);
        stats_us_ = now;
        presented_ = 0;
    }
}
#endif
