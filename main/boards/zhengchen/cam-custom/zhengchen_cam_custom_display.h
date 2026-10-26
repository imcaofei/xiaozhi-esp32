#ifndef ZHENGCHEN_CAM_CUSTOM_DISPLAY_H
#define ZHENGCHEN_CAM_CUSTOM_DISPLAY_H

#include "display/lcd_display.h"
#include "lvgl_theme.h"

#include "config.h"

#include <lvgl.h>

#include <cstdint>
#include <cstring>
#include <string>

#include <esp_heap_caps.h>

/**
 * LCD display with a home-screen info line (date + weather) and a
 * full-screen weather forecast page overlay.
 *
 * The info line sits just below the top status bar, left aligned. The
 * weather page is a full-screen overlay created on the same LVGL screen
 * (hidden by default); showing it covers the emoji / chat areas and the
 * top bar. Global popups such as the low-battery warning are kept above
 * the overlay.
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
     * Live camera preview: copy one RGB565 frame into a PSRAM buffer and
     * show it in a full-screen overlay. Called from the preview task;
     * thread-safe. Frames are expected at panel resolution (QVGA).
     */
    void ShowPreviewFrame(const void* data, size_t len, int width, int height) {
        if (!IsSetupUICalled() || data == nullptr) {
            return;
        }
        DisplayLockGuard lock(this);
        if (preview_img_ == nullptr) {
            CreatePreviewOverlayLocked();
        }
        if (preview_img_ == nullptr) {
            return;
        }

        const size_t needed = static_cast<size_t>(width) * height * 2;
        if (preview_buf_ == nullptr || preview_buf_size_ < needed) {
            if (preview_buf_ != nullptr) {
                heap_caps_free(preview_buf_);
            }
            preview_buf_ = static_cast<uint8_t*>(heap_caps_malloc(needed, MALLOC_CAP_SPIRAM));
            preview_buf_size_ = preview_buf_ != nullptr ? needed : 0;
            if (preview_buf_ == nullptr) {
                ESP_LOGE("CamCustomDisplay", "preview buffer alloc failed");
                return;
            }
        }

        const uint16_t* src = static_cast<const uint16_t*>(data);
        uint16_t* dst = reinterpret_cast<uint16_t*>(preview_buf_);
        const size_t count = needed / 2;
        (void)len;
#if PREVIEW_SWAP_BYTES
        for (size_t i = 0; i < count; ++i) {
            dst[i] = __builtin_bswap16(src[i]);
        }
#else
        memcpy(dst, src, needed);
#endif

        if (preview_dsc_.header.w != width || preview_dsc_.header.h != height) {
            preview_dsc_ = {};
            preview_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
            preview_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
            preview_dsc_.header.w = width;
            preview_dsc_.header.h = height;
            preview_dsc_.data_size = needed;
        }
        preview_dsc_.data = preview_buf_;
        lv_image_set_src(preview_img_, &preview_dsc_);
        lv_obj_clear_flag(preview_img_, LV_OBJ_FLAG_HIDDEN);
        preview_shown_ = true;
    }

    /** Close the live preview overlay. */
    void HidePreview() {
        DisplayLockGuard lock(this);
        if (preview_img_ != nullptr) {
            lv_obj_add_flag(preview_img_, LV_OBJ_FLAG_HIDDEN);
        }
        preview_shown_ = false;
    }

    /**
     * Fill one forecast column. index: 0 = yesterday, 1 = today, 2..: the
     * following days. Day and condition texts are rendered vertically
     * (one character per line); temps are split into high/low stacked
     * lines. Calls made before SetupUI() are dropped and the column stays
     * as a placeholder.
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
        lv_label_set_text(forecast_day_label_[index], VerticalText(day).c_str());
        lv_label_set_text(forecast_cond_label_[index], VerticalText(condition).c_str());
        lv_label_set_text(forecast_temp_label_[index], StackedTemps(temps).c_str());
    }

private:
    static constexpr int kForecastDays = WEATHER_FORECAST_DAYS;

    lv_obj_t* info_label_ = nullptr;
    lv_obj_t* weather_page_ = nullptr;
    lv_obj_t* preview_img_ = nullptr;
    uint8_t* preview_buf_ = nullptr;
    size_t preview_buf_size_ = 0;
    lv_image_dsc_t preview_dsc_ = {};
    bool preview_shown_ = false;
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

        weather_page_ = lv_obj_create(lv_screen_active());
        lv_obj_set_size(weather_page_, LV_HOR_RES, LV_VER_RES);
        lv_obj_align(weather_page_, LV_ALIGN_TOP_MID, 0, 0);
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

        // Column layout: title on top, then one vertical strip per day.
        lv_obj_set_flex_flow(weather_page_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_left(weather_page_, 4, 0);
        lv_obj_set_style_pad_right(weather_page_, 4, 0);
        lv_obj_set_style_pad_top(weather_page_, 4, 0);
        lv_obj_set_style_pad_bottom(weather_page_, 2, 0);
        lv_obj_set_style_pad_row(weather_page_, 2, 0);

        lv_obj_t* title = lv_label_create(weather_page_);
        lv_label_set_text(title, BOARD_CITY_NAME " · 天气预报");

        lv_obj_t* columns = lv_obj_create(weather_page_);
        lv_obj_set_width(columns, LV_PCT(100));
        lv_obj_set_flex_grow(columns, 1);
        lv_obj_set_style_radius(columns, 0, 0);
        lv_obj_set_style_border_width(columns, 0, 0);
        lv_obj_set_style_pad_all(columns, 0, 0);
        lv_obj_set_style_bg_opa(columns, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(columns, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);

        for (int i = 0; i < kForecastDays; ++i) {
            lv_obj_t* col = lv_obj_create(columns);
            lv_obj_set_height(col, LV_PCT(100));
            lv_obj_set_flex_grow(col, 1);
            lv_obj_set_style_radius(col, 0, 0);
            lv_obj_set_style_pad_all(col, 0, 0);
            lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
            lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
            if (i < kForecastDays - 1 && theme != nullptr) {
                lv_obj_set_style_border_width(col, 1, 0);
                lv_obj_set_style_border_side(col, LV_BORDER_SIDE_RIGHT, 0);
                lv_obj_set_style_border_color(col, theme->border_color(), 0);
            }

            lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);

            forecast_day_label_[i] = lv_label_create(col);
            lv_label_set_text(forecast_day_label_[i], "--");

            forecast_cond_label_[i] = lv_label_create(col);
            lv_label_set_text(forecast_cond_label_[i], "--");

            forecast_temp_label_[i] = lv_label_create(col);
            lv_label_set_text(forecast_temp_label_[i], "--°");
        }

        // The overlay is created last so it is topmost; raise global popups
        // (e.g. low battery) back above it.
        if (low_battery_popup_ != nullptr) {
            lv_obj_move_foreground(low_battery_popup_);
        }
    }

    /** Full-screen live camera overlay (topmost, below global popups). */
    void CreatePreviewOverlayLocked() {
        preview_img_ = lv_image_create(lv_screen_active());
        lv_obj_center(preview_img_);
        // Sensor frames are QVGA (320x240): exact match in landscape; rotate
        // 90 degrees around the image center for portrait so it fills the
        // 240x320 panel.
        if (LV_HOR_RES < LV_VER_RES) {
            lv_image_set_pivot(preview_img_, 160, 120);
            lv_image_set_rotation(preview_img_, 900);  // 90.0 degrees
        }
        lv_obj_add_flag(preview_img_, LV_OBJ_FLAG_HIDDEN);
        if (low_battery_popup_ != nullptr) {
            lv_obj_move_foreground(low_battery_popup_);
        }
    }

    /**
     * Insert a newline between UTF-8 characters so an LVGL label renders
     * the text vertically ("多云" -> "多\n云"). ASCII passes through.
     */
    static std::string VerticalText(const std::string& text) {
        std::string out;
        size_t i = 0;
        while (i < text.size()) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            size_t len = 1;
            if ((c & 0xE0) == 0xC0) {
                len = 2;
            } else if ((c & 0xF0) == 0xE0) {
                len = 3;
            } else if ((c & 0xF8) == 0xF0) {
                len = 4;
            }
            if (i > 0) {
                out += '\n';
            }
            out.append(text, i, len);
            i += len;
        }
        return out;
    }

    /** "14°/26°" (min/max) -> "26°\n14°" with the high temperature on top. */
    static std::string StackedTemps(const std::string& temps) {
        const size_t slash = temps.find('/');
        if (slash == std::string::npos) {
            return temps;
        }
        return temps.substr(slash + 1) + "\n" + temps.substr(0, slash);
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
