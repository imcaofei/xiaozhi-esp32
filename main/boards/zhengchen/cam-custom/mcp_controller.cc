#include <cJSON.h>
#include <esp_log.h>

#include <cstring>

#include "application.h"
#include "board.h"
#include "config.h"
#include "mcp_server.h"
#include "sdkconfig.h"
#include "settings.h"
#include "display.h"
#include "ui_controller.h"
#include "weather_service.h"
#include "camera_preview.h"
#include "zhengchen_cam_custom_display.h"

#define TAG "MCPController"

class MCPController {
public:
    MCPController() {
        RegisterMcpTools();
        ESP_LOGI(TAG, "注册MCP工具");
    }

	void RegisterMcpTools() {
        auto& mcp_server = McpServer::GetInstance();
		ESP_LOGI(TAG, "开始注册MCP工具...");

	mcp_server.AddTool(
        "self.AEC.set_mode", 
        "设置AEC对话打断模式。当用户意图切换对话打断模式时或者用户觉得ai对话容易被打断时或者用户觉得无法实现对话打断时都使用此工具。\n"
        "参数：\n"
        "   `mode`: 对话打断模式，可选值只有`kAecOff`(关闭）和`kAecOnDeviceSide`（开启）\n"
        "返回值：\n"
        "   反馈状态信息，不需要确认，立即播报相关数据\n",
        PropertyList({
            Property("mode", kPropertyTypeString)
        }), 
        [](const PropertyList& properties) -> ReturnValue {
            auto mode = properties["mode"].value<std::string>();
            auto& app = Application::GetInstance();
            vTaskDelay(pdMS_TO_TICKS(2000));
            if (mode == "kAecOff") {
                app.SetAecMode(kAecOff);
                return "{\"success\": true, \"message\": \"AEC对话打断模式已关闭\"}";
            }else {
                auto& board = Board::GetInstance();
                app.SetAecMode(kAecOnDeviceSide);
                
                return "{\"success\": true, \"message\": \"AEC对话打断模式已开启\"}";
            }
        }
    );

    mcp_server.AddTool(
        "self.AEC.get_mode",
        "获取AEC对话打断模式状态。当用户意图获取对话打断模式状态时使用此工具。\n"
        "返回值：\n"
        "   反馈状态信息，不需要确认，立即播报相关数据\n",
        PropertyList(),  
        [](const PropertyList&) -> ReturnValue {
            auto& app = Application::GetInstance();
            const bool is_currently_off = (app.GetAecMode() == kAecOff);
           if (is_currently_off) {
                return "{\"success\": true, \"message\": \"AEC对话打断模式处于关闭状态\"}";
            }else {
                return "{\"success\": true, \"message\": \"AEC对话打断模式处于开启状态\"}";
            }
        }
    );
	
    mcp_server.AddTool(
        "self.res.esp_restart",
        "重启设备。当用户意图重启设备时使用此工具。\n",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            vTaskDelay(pdMS_TO_TICKS(1000));
            // Reboot the device
            esp_restart();
            return true;
        }
    );

    mcp_server.AddTool(
        "self.ui.show_page",
        "切换设备屏幕显示的页面。当用户想查看屏幕上的天气预报页面、或想从天气页面回到主对话界面时使用此工具。\n"
        "参数：\n"
        "   `page`: 目标页面，可选值为`weather`（天气预报页面）和`main`（主对话页面）\n"
        "返回值：\n"
        "   反馈切换结果，不需要确认，立即播报相关数据\n",
        PropertyList({
            Property("page", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto page = properties["page"].value<std::string>();
            auto* ui = UiController::GetInstance();
            if (ui == nullptr) {
                return "{\"success\": false, \"message\": \"页面控制器还没有就绪，请稍后再试\"}";
            }
            if (!ui->ShowPage(page)) {
                return "{\"success\": false, \"message\": \"不支持的页面名称，可选值为weather和main\"}";
            }
            if (page == "weather" || page == "天气") {
                return "{\"success\": true, \"message\": \"已切换到天气预报页面\"}";
            }
            return "{\"success\": true, \"message\": \"已回到主对话页面\"}";
        }
    );

    mcp_server.AddTool(
        "self.weather.query",
        "查询天气预报并自动把屏幕切换到天气预报页面。当用户询问某个日期的天气时使用此工具，例如\"今天天气怎么样\"\"明天冷不冷\"\"后天会不会下雨\"\"周四天气如何\"。\n"
        "参数：\n"
        "   `day`: 要查询的日期，可选值为`昨天`、`今天`、`明天`、`后天`、`大后天`或星期名称（如`周三`、`星期五`），不传时默认查询`今天`\n"
        "返回值：\n"
        "   反馈天气数据，不需要确认，立即播报相关数据\n",
        PropertyList({
            Property("day", kPropertyTypeString, std::string("今天"))
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto& weather = WeatherService::GetInstance();
            WeatherService::ForecastInfo forecast;
            if (!weather.GetLastForecast(forecast)) {
                return "{\"success\": false, \"message\": \"天气数据还没有获取到，可能还没有联网，请稍后再问\"}";
            }

            auto day = properties["day"].value<std::string>();
            int index = weather.DayIndexFromName(day);
            bool known = index >= 0;
            if (index < 0) {
                index = 1;  // fall back to today
            }
            const auto& info = forecast.days[index];

            // temps is "min°/max°"; phrase it for speech.
            std::string temps_min = info.temps;
            std::string temps_max = "--";
            const size_t slash = info.temps.find('/');
            if (slash != std::string::npos) {
                temps_min = info.temps.substr(0, slash);
                temps_max = info.temps.substr(slash + 1);
            }

            // Bring up the weather page so screen and speech match. The page
            // stays visible through the current speaking turn.
            if (auto* ui = UiController::GetInstance(); ui != nullptr) {
                ui->RequestWeatherPage();
            }

            char buf[160];
            if (info.day_label == "今天") {
                WeatherService::WeatherInfo current;
                if (weather.GetLastWeather(current) && current.valid) {
                    snprintf(buf, sizeof(buf),
                             "{\"success\": true, \"message\": \"杭州现在%s，%d度，%s气温%s到%s度\"}",
                             current.condition.c_str(), current.temperature,
                             info.condition.c_str(), temps_min.c_str(), temps_max.c_str());
                    return buf;
                }
            }
            if (!known) {
                snprintf(buf, sizeof(buf),
                         "{\"success\": false, \"message\": \"没有查到该日期的天气，可以问今天、明天、后天或最近一周内的星期几\"}");
                return buf;
            }
            snprintf(buf, sizeof(buf),
                     "{\"success\": true, \"message\": \"杭州%s%s，气温%s到%s度\"}",
                     info.day_label.c_str(), info.condition.c_str(),
                     temps_min.c_str(), temps_max.c_str());
            return buf;
        }
    );

    mcp_server.AddTool(
        "self.camera.preview",
        "打开或关闭屏幕上的实时监控画面（摄像头取景器）。当用户想实时查看摄像头画面时使用，例如\"打开监控画面\"\"让我看看镜头前是什么\"\"打开实时画面\"；当用户要求关闭监控画面时传`off`。\n"
        "参数：\n"
        "   `action`: 可选值为`on`（打开实时画面）和`off`（关闭实时画面）\n"
        "返回值：\n"
        "   反馈操作结果，不需要确认，立即播报相关数据\n",
        PropertyList({
            Property("action", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto action = properties["action"].value<std::string>();
            auto& preview = CameraPreview::GetInstance();
            if (action == "on" || action == "open") {
                auto* display = static_cast<ZhengchenCamCustomDisplay*>(
                    Board::GetInstance().GetDisplay());
                if (preview.Start(display)) {
                    return "{\"success\": true, \"message\": \"监控画面已打开，屏幕上正在显示摄像头实时画面，观看完请告诉我关闭\"}";
                }
                return "{\"success\": false, \"message\": \"监控画面打开失败，请稍后再试\"}";
            }
            preview.Stop();
            return "{\"success\": true, \"message\": \"监控画面已关闭\"}";
        }
    );

        ESP_LOGI(TAG, "MCP工具注册完成");
    }

};

static MCPController* g_mcp_controller = nullptr;

void InitializeMCPController() {
    if (g_mcp_controller == nullptr) {
        g_mcp_controller = new MCPController();
        ESP_LOGI(TAG, "注册MCP工具");
    }
}