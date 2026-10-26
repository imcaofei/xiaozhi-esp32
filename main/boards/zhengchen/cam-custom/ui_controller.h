#ifndef ZHENGCHEN_CAM_CUSTOM_UI_CONTROLLER_H
#define ZHENGCHEN_CAM_CUSTOM_UI_CONTROLLER_H

#include <esp_timer.h>

#include <string>

class ZhengchenCamCustomDisplay;

/**
 * Decides when the display switches between the main chat page and the
 * weather forecast page.
 *
 * Rules:
 *  - after the device has been idle (no conversation) for
 *    UI_IDLE_SHOW_WEATHER_AFTER_MS, show the weather page;
 *  - as soon as any conversation activity starts (listening / speaking /
 *    ...), return to the main page;
 *  - a page requested explicitly (MCP tool) is kept through the current
 *    speaking turn; the next conversation after the device goes idle
 *    returns to normal automatic behaviour.
 *
 * A periodic timer polls the application state machine. MCP tools call
 * RequestWeatherPage() / RequestMainPage() through GetInstance().
 */
class UiController {
public:
    explicit UiController(ZhengchenCamCustomDisplay* display);
    ~UiController();

    /** The controller created by the board, or nullptr before that. */
    static UiController* GetInstance() { return instance_; }

    void Start();
    void Stop();

    /** Explicitly show the weather page (e.g. from an MCP tool). */
    void RequestWeatherPage();

    /** Explicitly return to the main chat page (e.g. from an MCP tool). */
    void RequestMainPage();

    /** "weather" -> weather page, anything else ("main"/"home") -> main. */
    bool ShowPage(const std::string& page);

private:
    static void PollTimerCallback(void* arg);
    void Poll();

    ZhengchenCamCustomDisplay* display_ = nullptr;
    esp_timer_handle_t poll_timer_ = nullptr;
    int64_t idle_since_us_ = -1;
    bool weather_page_shown_ = false;
    bool manual_hold_ = false;

    static UiController* instance_;
};

#endif  // ZHENGCHEN_CAM_CUSTOM_UI_CONTROLLER_H
