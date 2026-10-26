#ifndef WEATHER_SERVICE_H
#define WEATHER_SERVICE_H

#include "config.h"

#include <functional>
#include <string>

/**
 * Minimal weather service backed by Open-Meteo (no API key required).
 *
 * A background task polls the API while the station is connected:
 *  - first successful fetch as soon as Wi-Fi is up
 *  - refresh every WEATHER_UPDATE_INTERVAL_MS
 *  - retry every WEATHER_RETRY_INTERVAL_MS after a failure
 *
 * One request returns both the current conditions and a daily forecast
 * covering WEATHER_FORECAST_DAYS entries: yesterday, today and the next
 * five days.
 *
 * Callbacks are invoked from the background task, so consumers must be
 * thread-safe (the display subclass takes the LVGL lock internally).
 */
class WeatherService {
public:
    static constexpr int kForecastDays = WEATHER_FORECAST_DAYS;

    struct WeatherInfo {
        bool valid = false;
        int temperature = 0;   // Celsius, rounded
        std::string condition; // localised short description, e.g. "多云"
    };

    struct ForecastDay {
        std::string day_label; // "昨天" / "今天" / "明天" / "周一"..
        std::string condition; // localised short description
        std::string temps;     // e.g. "18°/26°" (min/max)
    };

    struct ForecastInfo {
        bool valid = false;
        ForecastDay days[kForecastDays];
    };

    using Callback = std::function<void(const WeatherInfo&)>;
    using ForecastCallback = std::function<void(const ForecastInfo&)>;

    static WeatherService& GetInstance();

    void SetCallback(Callback callback);
    void SetForecastCallback(ForecastCallback callback);

    /** Start the background polling task (idempotent). */
    void Start();

    /** Wake the polling task to fetch immediately (e.g. after network is ready). */
    void RequestUpdate();

private:
    WeatherService() = default;
    ~WeatherService() = default;
    WeatherService(const WeatherService&) = delete;
    WeatherService& operator=(const WeatherService&) = delete;

    static void TaskEntry(void* arg);
    void TaskLoop();
    bool Fetch(WeatherInfo& info, ForecastInfo& forecast);

    Callback callback_;
    ForecastCallback forecast_callback_;
    bool started_ = false;
    void* task_handle_ = nullptr;
};

#endif  // WEATHER_SERVICE_H
