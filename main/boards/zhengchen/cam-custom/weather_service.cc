#include "weather_service.h"
#include "config.h"

#include "board.h"
#include "http.h"
#include "network_interface.h"

#include <cJSON.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cmath>
#include <cstdio>
#include <string>

#define TAG "WeatherService"

namespace {

constexpr const char* kApiUrl =
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=" WEATHER_LATITUDE "&longitude=" WEATHER_LONGITUDE
    "&current=temperature_2m,weather_code&timezone=Asia%2FShanghai";

// Map WMO weather codes to short Chinese descriptions.
const char* ConditionFromCode(int code) {
    switch (code) {
        case 0:
            return "晴";
        case 1:
            return "晴间多云";
        case 2:
            return "多云";
        case 3:
            return "阴";
        case 45:
        case 48:
            return "雾";
        case 51:
        case 53:
        case 55:
            return "毛毛雨";
        case 56:
        case 57:
            return "冻雨";
        case 61:
            return "小雨";
        case 63:
            return "中雨";
        case 65:
            return "大雨";
        case 66:
        case 67:
            return "冻雨";
        case 71:
            return "小雪";
        case 73:
            return "中雪";
        case 75:
            return "大雪";
        case 77:
            return "米雪";
        case 80:
            return "阵雨";
        case 81:
            return "强阵雨";
        case 82:
            return "暴雨";
        case 85:
            return "阵雪";
        case 86:
            return "强阵雪";
        case 95:
            return "雷阵雨";
        case 96:
        case 99:
            return "雷阵雨伴冰雹";
        default:
            return "未知";
    }
}

}  // namespace

WeatherService& WeatherService::GetInstance() {
    static WeatherService instance;
    return instance;
}

void WeatherService::SetCallback(Callback callback) {
    callback_ = std::move(callback);
}

void WeatherService::Start() {
    if (started_) {
        return;
    }
    started_ = true;
    if (xTaskCreate(TaskEntry, "weather_svc", 8 * 1024, this, 3,
                    reinterpret_cast<TaskHandle_t*>(&task_handle_)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create weather task");
        started_ = false;
        task_handle_ = nullptr;
    }
}

void WeatherService::RequestUpdate() {
    if (task_handle_ != nullptr) {
        xTaskNotifyGive(reinterpret_cast<TaskHandle_t>(task_handle_));
    }
}

void WeatherService::TaskEntry(void* arg) {
    static_cast<WeatherService*>(arg)->TaskLoop();
    vTaskDelete(nullptr);
}

void WeatherService::TaskLoop() {
    while (true) {
        WeatherInfo info;
        const bool ok = Fetch(info);
        if (ok) {
            ESP_LOGI(TAG, "Weather: %s %d°C", info.condition.c_str(), info.temperature);
        } else {
            ESP_LOGW(TAG, "Failed to fetch weather");
        }

        if (callback_) {
            callback_(info);
        }

        // RequestUpdate() can wake the task early (e.g. after reconnect).
        const uint32_t delay_ms = ok ? WEATHER_UPDATE_INTERVAL_MS : WEATHER_RETRY_INTERVAL_MS;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms));
    }
}

bool WeatherService::Fetch(WeatherInfo& info) {
    NetworkInterface* network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        ESP_LOGW(TAG, "No network interface available");
        return false;
    }

    auto http = network->CreateHttp(0);
    if (http == nullptr) {
        ESP_LOGW(TAG, "Failed to create HTTP client");
        return false;
    }

    http->SetTimeout(10000);
    http->SetHeader("Accept", "application/json");

    if (auto opened = http->Open("GET", kApiUrl); !opened) {
        ESP_LOGW(TAG, "Failed to open connection: %s", opened.error().ToString().c_str());
        return false;
    }

    auto status_code = http->GetStatusCode();
    if (!status_code || *status_code != 200) {
        ESP_LOGW(TAG, "Unexpected HTTP status: %d",
                 status_code ? *status_code : -1);
        http->Close();
        return false;
    }

    std::string body = http->ReadAll();
    http->Close();

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        ESP_LOGW(TAG, "Failed to parse JSON: %s", body.c_str());
        return false;
    }

    bool ok = false;
    const cJSON* current = cJSON_GetObjectItem(root, "current");
    if (current != nullptr) {
        const cJSON* temperature = cJSON_GetObjectItem(current, "temperature_2m");
        const cJSON* weather_code = cJSON_GetObjectItem(current, "weather_code");
        if (cJSON_IsNumber(temperature) && cJSON_IsNumber(weather_code)) {
            info.valid = true;
            info.temperature = static_cast<int>(std::lround(temperature->valuedouble));
            info.condition = ConditionFromCode(weather_code->valueint);
            ok = true;
        }
    }

    cJSON_Delete(root);
    return ok;
}
