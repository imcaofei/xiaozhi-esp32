#ifndef ZHENGCHEN_CAM_CUSTOM_UI_CONTROLLER_H
#define ZHENGCHEN_CAM_CUSTOM_UI_CONTROLLER_H

#include <esp_timer.h>

class ZhengchenCamCustomDisplay;

/**
 * Decides when the display switches between the main chat page and the
 * weather forecast page.
 *
 * Rule: after the device has been idle (no conversation) for
 * UI_IDLE_SHOW_WEATHER_AFTER_MS, show the weather page; as soon as any
 * conversation activity starts (listening / speaking / ...), return to the
 * main page. A periodic timer polls the application state machine.
 */
class UiController {
public:
    explicit UiController(ZhengchenCamCustomDisplay* display);
    ~UiController();

    void Start();
    void Stop();

private:
    static void PollTimerCallback(void* arg);
    void Poll();

    ZhengchenCamCustomDisplay* display_ = nullptr;
    esp_timer_handle_t poll_timer_ = nullptr;
    int64_t idle_since_us_ = -1;
    bool weather_page_shown_ = false;
};

#endif  // ZHENGCHEN_CAM_CUSTOM_UI_CONTROLLER_H
