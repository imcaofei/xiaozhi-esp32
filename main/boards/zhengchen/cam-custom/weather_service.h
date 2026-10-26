#ifndef WEATHER_SERVICE_H
#define WEATHER_SERVICE_H

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
 * The callback is invoked from the background task, so consumers must be
 * thread-safe (the display subclass takes the LVGL lock internally).
 */
class WeatherService {
public:
    struct WeatherInfo {
        bool valid = false;
        int temperature = 0;   // Celsius, rounded
        std::string condition; // localised short description, e.g. "多云"
    };

    using Callback = std::function<void(const WeatherInfo&)>;

    static WeatherService& GetInstance();

    void SetCallback(Callback callback);

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
    bool Fetch(WeatherInfo& info);

    Callback callback_;
    bool started_ = false;
    void* task_handle_ = nullptr;
};

#endif  // WEATHER_SERVICE_H
