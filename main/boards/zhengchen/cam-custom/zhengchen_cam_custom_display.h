#ifndef ZHENGCHEN_CAM_CUSTOM_DISPLAY_H
#define ZHENGCHEN_CAM_CUSTOM_DISPLAY_H

#include "display/lcd_display.h"
#include "lvgl_theme.h"

#include <lvgl.h>

#include <string>

/**
 * LCD display with a home-screen info line (date + weather).
 *
 * The label sits just below the top status bar, left aligned, and leaves the
 * centre clock, the emoji area, the chat subtitle bar and the top-bar icons
 * untouched. Text updates are thread-safe: they take the LVGL lock.
 */
class ZhengchenCamCustomDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;

    void SetupUI() override {
        SpiLcdDisplay::SetupUI();

        DisplayLockGuard lock(this);
        if (info_label_ != nullptr) {
            return;
        }

        CreateInfoLabel();
        RefreshLocked();
    }

    void SetTheme(Theme* theme) override {
        SpiLcdDisplay::SetTheme(theme);

        DisplayLockGuard lock(this);
        if (info_label_ == nullptr) {
            return;
        }
        ApplyTheme(info_label_);
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

private:
    lv_obj_t* info_label_ = nullptr;
    std::string date_text_;
    std::string weather_text_;

    void CreateInfoLabel() {
        if (top_bar_ == nullptr) {
            ESP_LOGW("CamCustomDisplay", "top_bar_ is null, skip info label");
            return;
        }

        // Make sure the top bar has been laid out so its height is known.
        lv_obj_update_layout(lv_screen_active());
        const int y = lv_obj_get_y(top_bar_) + lv_obj_get_height(top_bar_);

        auto* theme = static_cast<LvglTheme*>(current_theme_);
        info_label_ = lv_label_create(lv_screen_active());
        ApplyTheme(info_label_);
        lv_obj_align(info_label_, LV_ALIGN_TOP_LEFT, theme->spacing(4), y + theme->spacing(1));
    }

    void ApplyTheme(lv_obj_t* label) {
        auto* theme = static_cast<LvglTheme*>(current_theme_);
        if (theme == nullptr || label == nullptr) {
            return;
        }
        if (theme->text_font() != nullptr) {
            lv_obj_set_style_text_font(label, theme->text_font()->font(), 0);
        }
        lv_obj_set_style_text_color(label, theme->text_color(), 0);
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
