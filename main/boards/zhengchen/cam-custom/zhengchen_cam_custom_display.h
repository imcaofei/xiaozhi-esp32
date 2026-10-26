#ifndef ZHENGCHEN_CAM_CUSTOM_DISPLAY_H
#define ZHENGCHEN_CAM_CUSTOM_DISPLAY_H

#include "display/lcd_display.h"
#include "lvgl_theme.h"

#include "config.h"

#include <lvgl.h>

#include <string>

/**
 * LCD display with a home-screen info line (date + weather) and a
 * full-screen weather forecast page overlay.
 *
 * The info line sits just below the top status bar, left aligned. The
 * weather page is a full-screen overlay created on the same LVGL screen
 * (hidden by default); showing it covers the emoji / chat areas but keeps
 * the top bar (clock, wifi, battery) visible. Global popups such as the
 * low-battery warning are kept above the overlay.
 *
 * All public methods are thread-safe: they take the LVGL lock.
 */
class ZhengchenCamCustomDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;

    void SetupUI() override {
        SpiLcdDisplay::SetupUI();

        DisplayLockGuard lock(this);
        if (info_label_ == nullptr) {
            CreateInfoLabel();
        }
        if (weather_page_ == nullptr) {
            CreateWeatherPage();
        }
        RefreshLocked();
        if (weather_page_requested_) {
            ShowWeatherPageLocked();
        }
    }

    void SetTheme(Theme* theme) override {
        SpiLcdDisplay::SetTheme(theme);

        DisplayLockGuard lock(this);
        if (info_label_ != nullptr) {
            ApplyTextTheme(info_label_);
        }
        if (weather_page_ != nullptr) {
            auto* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
            if (lvgl_theme != nullptr) {
                lv_obj_set_style_bg_color(weather_page_, lvgl_theme->background_color(), 0);
            }
            ApplyTextTheme(weather_page_);
        }
    }

    /** Left part of the info line, e.g. "10-26 周一". Empty hides it. */
    void UpdateDateTime(const std::string& text) {
        if (!IsSetupUICalled()) {
            date_text_ = text;
            return;
        }
        {
            DisplayLockGuard lock(this);
            date_text_ = text;
            RefreshLocked();
        }
    }

    /** Right part of the info line, e.g. "多云 26°". Empty hides it. */
    void UpdateWeather(const std::string& text) {
        if (!IsSetupUICalled()) {
            weather_text_ = text;
            return;
        }
        {
            DisplayLockGuard lock(this);
            weather_text_ = text;
            RefreshLocked();
        }
    }

    /** Show the full-screen weather forecast overlay. */
    void ShowWeatherPage() {
        if (!IsSetupUICalled()) {
            weather_page_requested_ = true;
            return;
        }
        DisplayLockGuard lock(this);
        ShowWeatherPageLocked();
    }

    /** Return to the main chat page. */
    void ShowMainPage() {
        weather_page_requested_ = false;
        if (!IsSetupUICalled()) {
            return;
        }
        DisplayLockGuard lock(this);
        ShowMainPageLocked();
    }

    bool IsWeatherPageShown() const { return weather_page_shown_; }

    /**
     * Fill one forecast row. index: 0 = today, 1 = tomorrow, 2 = day after.
     * Calls made before SetupUI() are dropped and the row stays as a
     * placeholder.
     */
    void UpdateForecastItem(int index, const std::string& day,
                            const std::string& condition, const std::string& temps) {
        if (index < 0 || index >= kForecastDays) {
            return;
        }
        DisplayLockGuard lock(this);
        if (forecast_day_label_[index] == nullptr) {
            return;
        }
        lv_label_set_text(forecast_day_label_[index], day.c_str());
        lv_label_set_text(forecast_cond_label_[index], condition.c_str());
        lv_label_set_text(forecast_temp_label_[index], temps.c_str());
    }

private:
    static constexpr int kForecastDays = 3;

    lv_obj_t* info_label_ = nullptr;
    lv_obj_t* weather_page_ = nullptr;
    lv_obj_t* forecast_day_label_[kForecastDays] = {};
    lv_obj_t* forecast_cond_label_[kForecastDays] = {};
    lv_obj_t* forecast_temp_label_[kForecastDays] = {};
    std::string date_text_;
    std::string weather_text_;
    bool weather_page_shown_ = false;
    bool weather_page_requested_ = false;

    int TopBarBottom() {
        if (top_bar_ == nullptr) {
            return 0;
        }
        lv_obj_update_layout(lv_screen_active());
        return lv_obj_get_y(top_bar_) + lv_obj_get_height(top_bar_);
    }

    void CreateInfoLabel() {
        if (top_bar_ == nullptr) {
            ESP_LOGW("CamCustomDisplay", "top_bar_ is null, skip info label");
            return;
        }
        const int y = TopBarBottom();

        info_label_ = lv_label_create(lv_screen_active());
        ApplyTextTheme(info_label_);
        auto* theme = static_cast<LvglTheme*>(current_theme_);
        lv_obj_align(info_label_, LV_ALIGN_TOP_LEFT, theme->spacing(4), y + theme->spacing(1));
    }

    void CreateWeatherPage() {
        auto* theme = static_cast<LvglTheme*>(current_theme_);
        const int top_h = TopBarBottom();

        weather_page_ = lv_obj_create(lv_screen_active());
        lv_obj_set_size(weather_page_, LV_HOR_RES, LV_VER_RES - top_h);
        lv_obj_align(weather_page_, LV_ALIGN_TOP_MID, 0, top_h);
        lv_obj_set_style_radius(weather_page_, 0, 0);
        lv_obj_set_style_border_width(weather_page_, 0, 0);
        lv_obj_set_style_pad_all(weather_page_, 0, 0);
        lv_obj_set_style_bg_opa(weather_page_, LV_OPA_COVER, 0);
        if (theme != nullptr) {
            lv_obj_set_style_bg_color(weather_page_, theme->background_color(), 0);
        }
        ApplyTextTheme(weather_page_);
        lv_obj_clear_flag(weather_page_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(weather_page_, LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_flex_flow(weather_page_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_left(weather_page_, 8, 0);
        lv_obj_set_style_pad_right(weather_page_, 8, 0);
        lv_obj_set_style_pad_top(weather_page_, 6, 0);
        lv_obj_set_style_pad_row(weather_page_, theme != nullptr ? theme->spacing(2) : 4, 0);

        lv_obj_t* title = lv_label_create(weather_page_);
        lv_label_set_text(title, BOARD_CITY_NAME " · 天气预报");

        static const char* kDayNames[kForecastDays] = {"今天", "明天", "后天"};
        for (int i = 0; i < kForecastDays; ++i) {
            lv_obj_t* row = lv_obj_create(weather_page_);
            lv_obj_set_size(row, LV_PCT(100), 52);
            lv_obj_set_style_radius(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            if (i < kForecastDays - 1 && theme != nullptr) {
                lv_obj_set_style_border_width(row, 1, 0);
                lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
                lv_obj_set_style_border_color(row, theme->border_color(), 0);
            }

            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);

            forecast_day_label_[i] = lv_label_create(row);
            lv_label_set_text(forecast_day_label_[i], kDayNames[i]);

            forecast_cond_label_[i] = lv_label_create(row);
            lv_label_set_text(forecast_cond_label_[i], "--");

            forecast_temp_label_[i] = lv_label_create(row);
            lv_label_set_text(forecast_temp_label_[i], "--° / --°");
        }

        // The overlay is created last so it is topmost; raise global popups
        // (e.g. low battery) back above it.
        if (low_battery_popup_ != nullptr) {
            lv_obj_move_foreground(low_battery_popup_);
        }
    }

    void ShowWeatherPageLocked() {
        if (weather_page_ == nullptr) {
            return;
        }
        lv_obj_clear_flag(weather_page_, LV_OBJ_FLAG_HIDDEN);
        if (low_battery_popup_ != nullptr) {
            lv_obj_move_foreground(low_battery_popup_);
        }
        weather_page_shown_ = true;
    }

    void ShowMainPageLocked() {
        if (weather_page_ == nullptr) {
            return;
        }
        lv_obj_add_flag(weather_page_, LV_OBJ_FLAG_HIDDEN);
        weather_page_shown_ = false;
    }

    void ApplyTextTheme(lv_obj_t* obj) {
        auto* theme = static_cast<LvglTheme*>(current_theme_);
        if (theme == nullptr || obj == nullptr) {
            return;
        }
        if (theme->text_font() != nullptr) {
            lv_obj_set_style_text_font(obj, theme->text_font()->font(), 0);
        }
        lv_obj_set_style_text_color(obj, theme->text_color(), 0);
    }

    void RefreshLocked() {
        if (info_label_ == nullptr) {
            return;
        }

        std::string text = date_text_;
        if (!weather_text_.empty()) {
            if (!text.empty()) {
                text += "  ";
            }
            text += weather_text_;
        }
        lv_label_set_text(info_label_, text.c_str());
    }
};

#endif  // ZHENGCHEN_CAM_CUSTOM_DISPLAY_H
