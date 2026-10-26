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
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>

#define TAG "WeatherService"

namespace {

constexpr const char* kApiUrl =
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=" WEATHER_LATITUDE "&longitude=" WEATHER_LONGITUDE
    "&current=temperature_2m,weather_code"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min"
    "&past_days=1&forecast_days=6"
    "&timezone=Asia%2FShanghai";

// Map WMO weather codes to short Chinese descriptions.
const char* ConditionFromCode(int code) {
    switch (code) {
        case 0:
            return "晴";
        case 1:
            return "少云";
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
            return "冰雹";
        default:
            return "未知";
    }
}

// Label for one daily entry: relative names for the near days, the weekday
// name for the rest. date is "YYYY-MM-DD" in local time (Open-Meteo).
std::string DayLabel(int index, const char* date) {
    static const char* kRelative[] = {"昨天", "今天", "明天"};
    if (index >= 0 && index < 3) {
        return kRelative[index];
    }

    int y = 0, m = 0, d = 0;
    if (sscanf(date, "%d-%d-%d", &y, &m, &d) != 3) {
        return "--";
    }
    std::tm tm_value = {};
    tm_value.tm_year = y - 1900;
    tm_value.tm_mon = m - 1;
    tm_value.tm_mday = d;
    if (mktime(&tm_value) < 0) {
        return "--";
    }
    static const char* kWeekdays[] = {"周日", "周一", "周二", "周三",
                                      "周四", "周五", "周六"};
    return kWeekdays[tm_value.tm_wday % 7];
}

}  // namespace

WeatherService& WeatherService::GetInstance() {
    static WeatherService instance;
    return instance;
}

void WeatherService::SetCallback(Callback callback) {
    callback_ = std::move(callback);
}

void WeatherService::SetForecastCallback(ForecastCallback callback) {
    forecast_callback_ = std::move(callback);
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

bool WeatherService::GetLastWeather(WeatherInfo& out) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    if (!has_weather_) {
        return false;
    }
    out = last_weather_;
    return true;
}

bool WeatherService::GetLastForecast(ForecastInfo& out) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    if (!has_forecast_) {
        return false;
    }
    out = last_forecast_;
    return true;
}

int WeatherService::DayIndexFromName(const std::string& name) {
    static const char* kRelative[] = {"昨天", "今天", "明天"};
    for (int i = 0; i < 3; ++i) {
        if (name == kRelative[i]) {
            return i;
        }
    }
    if (name == "后天") {
        return 3;
    }
    if (name == "大后天") {
        return 4;
    }

    // Weekday names: normalize "星期X" to "周X" (each CJK char is 3 bytes
    // in UTF-8, so "星期" occupies 6 bytes) and match against the cached
    // forecast labels.
    std::string normalized = name;
    size_t pos;
    while ((pos = normalized.find("星期")) != std::string::npos) {
        normalized.replace(pos, 6, "周");
    }
    std::lock_guard<std::mutex> lock(cache_mutex_);
    if (!has_forecast_) {
        return -1;
    }
    for (int i = 0; i < kForecastDays; ++i) {
        if (last_forecast_.days[i].day_label == normalized) {
            return i;
        }
    }
    return -1;
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
        ForecastInfo forecast;
        const bool ok = Fetch(info, forecast);
        if (ok) {
            ESP_LOGI(TAG, "Weather: %s %d°C, forecast %s",
                     info.condition.c_str(), info.temperature,
                     forecast.valid ? "updated" : "unavailable");
        } else {
            ESP_LOGW(TAG, "Failed to fetch weather");
        }

        if (callback_) {
            callback_(info);
        }
        if (ok && forecast_callback_ && forecast.valid) {
            forecast_callback_(forecast);
        }

        // Cache the latest results for synchronous consumers (MCP tools).
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            if (info.valid) {
                last_weather_ = info;
                has_weather_ = true;
            }
            if (forecast.valid) {
                last_forecast_ = forecast;
                has_forecast_ = true;
            }
        }

        // RequestUpdate() can wake the task early (e.g. after reconnect).
        const uint32_t delay_ms = ok ? WEATHER_UPDATE_INTERVAL_MS : WEATHER_RETRY_INTERVAL_MS;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms));
    }
}

bool WeatherService::Fetch(WeatherInfo& info, ForecastInfo& forecast) {
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

    // Daily forecast: past_days=1 + forecast_days=6 => 7 entries starting
    // with yesterday. Entries beyond the array size are ignored.
    const cJSON* daily = cJSON_GetObjectItem(root, "daily");
    const cJSON* daily_time = daily ? cJSON_GetObjectItem(daily, "time") : nullptr;
    const cJSON* daily_code = daily ? cJSON_GetObjectItem(daily, "weather_code") : nullptr;
    const cJSON* daily_tmax = daily ? cJSON_GetObjectItem(daily, "temperature_2m_max") : nullptr;
    const cJSON* daily_tmin = daily ? cJSON_GetObjectItem(daily, "temperature_2m_min") : nullptr;
    if (daily_time != nullptr && daily_code != nullptr && daily_tmax != nullptr &&
        daily_tmin != nullptr && cJSON_IsArray(daily_time)) {
        const int count = cJSON_GetArraySize(daily_time);
        int filled = 0;
        for (int i = 0; i < count && filled < kForecastDays; ++i) {
            const cJSON* time_item = cJSON_GetArrayItem(daily_time, i);
            const cJSON* code_item = cJSON_GetArrayItem(daily_code, i);
            const cJSON* tmax_item = cJSON_GetArrayItem(daily_tmax, i);
            const cJSON* tmin_item = cJSON_GetArrayItem(daily_tmin, i);
            if (!cJSON_IsString(time_item) || !cJSON_IsNumber(code_item) ||
                !cJSON_IsNumber(tmax_item) || !cJSON_IsNumber(tmin_item)) {
                continue;
            }
            ForecastDay& day = forecast.days[filled];
            day.day_label = DayLabel(filled, time_item->valuestring);
            day.condition = ConditionFromCode(code_item->valueint);
            char temps[24];
            snprintf(temps, sizeof(temps), "%d°/%d°",
                     static_cast<int>(std::lround(tmin_item->valuedouble)),
                     static_cast<int>(std::lround(tmax_item->valuedouble)));
            day.temps = temps;
            ++filled;
        }
        if (filled == kForecastDays) {
            forecast.valid = true;
        } else {
            ESP_LOGW(TAG, "Incomplete daily forecast: %d/%d entries", filled, kForecastDays);
        }
    }

    cJSON_Delete(root);
    return ok;
}
