#include "camera_preview.h"

#include "zhengchen_cam_custom_display.h"
#include "config.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_camera.h>

static const char* TAG = "CameraPreview";

CameraPreview& CameraPreview::GetInstance() {
    static CameraPreview instance;
    return instance;
}

bool CameraPreview::Start(ZhengchenCamCustomDisplay* display) {
    if (running_) {
        return true;
    }
    if (display == nullptr) {
        return false;
    }

    // Switch the sensor to QVGA so the frame matches the LCD exactly
    // (no software scaling). Restored on Stop().
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor != nullptr) {
        sensor->set_framesize(sensor, FRAMESIZE_QVGA);
    }

    display_ = display;
    done_ = false;
    running_ = true;
    if (xTaskCreate(TaskEntry, "cam_preview", 4 * 1024, this, 3, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create preview task");
        running_ = false;
        done_ = true;
        return false;
    }
    ESP_LOGI(TAG, "preview started");
    return true;
}

void CameraPreview::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    // The task tears the sensor and overlay down itself; give it a moment.
    for (int i = 0; i < 50 && !done_; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "preview stopped");
}

void CameraPreview::TaskEntry(void* arg) {
    static_cast<CameraPreview*>(arg)->TaskLoop();
    vTaskDelete(nullptr);
}

void CameraPreview::TaskLoop() {
    sensor_t* sensor = nullptr;
    const TickType_t retry_delay = pdMS_TO_TICKS(20);
    const int64_t start_us = esp_timer_get_time();

    while (running_) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb == nullptr) {
            // Both frame buffers busy (e.g. AI photo capture in progress)
            vTaskDelay(retry_delay);
            continue;
        }
        if (fb->format == PIXFORMAT_RGB565 && display_ != nullptr) {
            display_->ShowPreviewFrame(fb->buf, fb->len, fb->width, fb->height);
        }
        esp_camera_fb_return(fb);

        if (esp_timer_get_time() - start_us >
            static_cast<int64_t>(PREVIEW_AUTO_STOP_MS) * 1000) {
            ESP_LOGI(TAG, "preview auto stopped after timeout");
            break;
        }
        // Pacing comes from the sensor frame rate; just yield.
        vTaskDelay(1);
    }

    sensor = esp_camera_sensor_get();
    if (sensor != nullptr) {
        sensor->set_framesize(sensor, FRAMESIZE_VGA);
    }
    if (display_ != nullptr) {
        display_->HidePreview();
    }
    running_ = false;
    done_ = true;
}
