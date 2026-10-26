#include "ui_controller.h"

#include "zhengchen_cam_custom_display.h"
#include "application.h"
#include "config.h"
#include "device_state.h"
#include "device_state_machine.h"

#include <esp_log.h>

static const char* TAG = "UiController";

UiController* UiController::instance_ = nullptr;

UiController::UiController(ZhengchenCamCustomDisplay* display) : display_(display) {
    instance_ = this;
}

UiController::~UiController() {
    if (instance_ == this) {
        instance_ = nullptr;
    }
    Stop();
}

void UiController::Start() {
    if (poll_timer_ != nullptr) {
        return;
    }
    esp_timer_create_args_t args = {};
    args.callback = PollTimerCallback;
    args.arg = this;
    args.name = "ui_page_poll";
    ESP_ERROR_CHECK(esp_timer_create(&args, &poll_timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(poll_timer_, UI_PAGE_POLL_INTERVAL_MS * 1000ULL));
}

void UiController::Stop() {
    if (poll_timer_ != nullptr) {
        esp_timer_stop(poll_timer_);
        esp_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
}

void UiController::RequestWeatherPage() {
    if (display_ == nullptr) {
        return;
    }
    ESP_LOGI(TAG, "manual request: weather page");
    display_->ShowWeatherPage();
    weather_page_shown_ = true;
    // Keep the page through the current speaking turn; Poll() releases the
    // hold once the device is idle again.
    manual_hold_ = true;
}

void UiController::RequestMainPage() {
    if (display_ == nullptr) {
        return;
    }
    ESP_LOGI(TAG, "manual request: main page");
    display_->ShowMainPage();
    weather_page_shown_ = false;
    // Don't let Poll() override the choice until the device is idle again.
    manual_hold_ = true;
}

bool UiController::ShowPage(const std::string& page) {
    if (page == "weather" || page == "天气") {
        RequestWeatherPage();
        return true;
    }
    if (page == "main" || page == "home" || page == "主页" || page == "对话") {
        RequestMainPage();
        return true;
    }
    return false;
}

void UiController::PollTimerCallback(void* arg) {
    static_cast<UiController*>(arg)->Poll();
}

void UiController::Poll() {
    if (display_ == nullptr) {
        return;
    }

    DeviceState state = Application::GetInstance().GetDeviceState();
    int64_t now = esp_timer_get_time();

    if (state == kDeviceStateIdle) {
        if (manual_hold_) {
            // The explicitly requested page survived the speaking turn;
            // resume automatic behaviour from now on.
            manual_hold_ = false;
            idle_since_us_ = now;
            return;
        }
        if (idle_since_us_ < 0) {
            idle_since_us_ = now;
        }
        if (!weather_page_shown_ &&
            now - idle_since_us_ >= static_cast<int64_t>(UI_IDLE_SHOW_WEATHER_AFTER_MS) * 1000) {
            ESP_LOGI(TAG, "idle for %d ms, show weather page", UI_IDLE_SHOW_WEATHER_AFTER_MS);
            display_->ShowWeatherPage();
            weather_page_shown_ = true;
        }
    } else {
        idle_since_us_ = -1;
        if (weather_page_shown_ && !manual_hold_) {
            ESP_LOGI(TAG, "device active (%s), back to main page",
                     DeviceStateMachine::GetStateName(state));
            display_->ShowMainPage();
            weather_page_shown_ = false;
        }
    }
}
