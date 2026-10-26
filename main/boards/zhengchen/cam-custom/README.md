# 产品相关介绍网址

## 简介
征辰科技 AI camera 是小智AI的魔改项目，做了大量创新和优化。

本目录 `zhengchen/cam-custom` 是基于 `zhengchen/cam` 派生的**自定义板型**，硬件与 `zhengchen/cam` 完全一致，
仅用于在该板型上开发自定义功能（首页日期天气显示等）。使用独立的板型名称 `zhengchen-cam-custom`
可以让 OTA 走独立通道，避免被官方固件覆盖。

## 合并版
合并版代码在小智AI主项目中维护，跟随主项目的一起版本更新，便于用户自行扩展和第三方固件扩展。支持语音唤醒、语音打断、OTA等功能。

## 魔改版
魔改版由于底层改动太大，代码单独维护，定期合并主项目代码。

https://e.tb.cn/h.6Gl2LC7rsrswQZp?tk=qFuaV9hzh0k CZ356
```
【淘宝】 「小智AI带摄像头支持识物双麦克风打断 ESP32S3N16R8开发板表情包」
https://e.tb.cn/h.hBc8Gcx9cUluJJO?tk=YW5C4LPixKg



## 配置、编译命令

由于此项目需要配置较多的 sdkconfig 选项，推荐使用编译脚本编译。

**编译**

```bash
python ./scripts/build.py zhengchen/cam-custom
```

如需手动编译，请参考 `zhengchen/cam-custom/config.json` 修改 menuconfig 对应选项。

**烧录**

```bash
idf.py flash


```

MCP Tool：
self.get_device_status
self.audio_speaker.set_volume
self.screen.set_brightness
self.screen.set_theme
self.gif.set_gif_mode
self.display.set_mode
self.camera.take_photo       
self.AEC.set_mode
self.AEC.get_mode
self.res.esp_restart