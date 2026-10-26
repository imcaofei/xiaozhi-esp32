#ifndef ZHENGCHEN_CAM_CUSTOM_CAMERA_PREVIEW_H
#define ZHENGCHEN_CAM_CUSTOM_CAMERA_PREVIEW_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class ZhengchenCamCustomDisplay;

/**
 * Live camera preview on the built-in LCD ("viewfinder" mode).
 *
 * A dedicated task continuously grabs RGB565 frames from the camera and
 * pushes them into a full-screen image overlay owned by the display class.
 * Start() switches the sensor to QVGA (320x240) so frames match the panel
 * without software scaling; Stop() restores the original framesize.
 *
 * The board uses fb_count=2 so that the framework's Capture() (AI photo)
 * can still grab a frame while the preview holds one.
 */
class CameraPreview {
public:
    static CameraPreview& GetInstance();

    /** Start pushing frames to the display overlay (idempotent). */
    bool Start(ZhengchenCamCustomDisplay* display);

    /** Stop the preview task, restore the sensor, hide the overlay. */
    void Stop();

    bool IsRunning() const { return running_; }

private:
    CameraPreview() = default;

    static void TaskEntry(void* arg);
    void TaskLoop();

    ZhengchenCamCustomDisplay* display_ = nullptr;
    volatile bool running_ = false;
    volatile bool done_ = true;
};

#endif  // ZHENGCHEN_CAM_CUSTOM_CAMERA_PREVIEW_H
