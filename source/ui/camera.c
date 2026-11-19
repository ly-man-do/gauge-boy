#include "camera.h"

#include <3ds.h>
#include <string.h>

#define FRAME_BYTES (CAR_PHOTO_CAM_W * CAR_PHOTO_CAM_H * 2)

static bool on;
static Handle event;
static u32 transfer;
static uint16_t buf[CAR_PHOTO_CAM_W * CAR_PHOTO_CAM_H] __attribute__((aligned(0x80)));

static bool receive(void)
{
    return R_SUCCEEDED(CAMU_SetReceiving(&event, buf, PORT_CAM1, FRAME_BYTES, (s16)transfer));
}

bool camera_start(void)
{
    if (on) {
        return true;
    }
    if (R_FAILED(camInit())) {
        return false;
    }
    bool ok = R_SUCCEEDED(CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A)) &&
              R_SUCCEEDED(CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A)) &&
              R_SUCCEEDED(CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_15)) &&
              R_SUCCEEDED(CAMU_SetNoiseFilter(SELECT_OUT1, true)) &&
              R_SUCCEEDED(CAMU_SetAutoExposure(SELECT_OUT1, true)) &&
              R_SUCCEEDED(CAMU_SetAutoWhiteBalance(SELECT_OUT1, true)) &&
              R_SUCCEEDED(CAMU_SetTrimming(PORT_CAM1, false)) &&
              R_SUCCEEDED(CAMU_GetMaxBytes(&transfer, CAR_PHOTO_CAM_W, CAR_PHOTO_CAM_H)) &&
              R_SUCCEEDED(CAMU_SetTransferBytes(PORT_CAM1, transfer, CAR_PHOTO_CAM_W,
                                                CAR_PHOTO_CAM_H)) &&
              R_SUCCEEDED(CAMU_Activate(SELECT_OUT1)) && R_SUCCEEDED(CAMU_ClearBuffer(PORT_CAM1)) &&
              R_SUCCEEDED(CAMU_StartCapture(PORT_CAM1)) && receive();
    if (!ok) {
        CAMU_Activate(SELECT_NONE);
        camExit();
        return false;
    }
    on = true;
    return true;
}

bool camera_poll(uint16_t *frame)
{
    if (!on || svcWaitSynchronization(event, 0) != 0) {
        return false;
    }
    svcCloseHandle(event);
    memcpy(frame, buf, FRAME_BYTES);
    if (!receive()) {
        event = 0;
    }
    return true;
}

void camera_stop(void)
{
    if (!on) {
        return;
    }
    CAMU_StopCapture(PORT_CAM1);
    if (event != 0) {
        svcCloseHandle(event);
        event = 0;
    }
    CAMU_Activate(SELECT_NONE);
    camExit();
    on = false;
}
