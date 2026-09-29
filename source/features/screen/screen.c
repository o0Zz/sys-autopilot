#include "features/screen/screen.h"
#include "core/log.h"

#include <strings.h>

#define CAPTURE_TIMEOUT_NS 100000000ULL // 100ms

Result screen_capture_jpeg(ViLayerStack stack, u8 *buf, size_t buf_size, u64 *out_size) {
    u64 size = 0;
    Result rc = capsscCaptureJpegScreenShot(&size, buf, buf_size, stack, CAPTURE_TIMEOUT_NS);
    if (R_FAILED(rc)) {
        LOGE("screen", "capture failed rc=0x%x", rc);
        return rc;
    }
    *out_size = size;
    return 0;
}

bool screen_parse_stack(const char *name, ViLayerStack *out) {
    if (strcasecmp(name, "screenshot") == 0)     *out = ViLayerStack_Screenshot;
    else if (strcasecmp(name, "default") == 0)   *out = ViLayerStack_Default;
    else if (strcasecmp(name, "lcd") == 0)       *out = ViLayerStack_Lcd;
    else if (strcasecmp(name, "recording") == 0) *out = ViLayerStack_Recording;
    else if (strcasecmp(name, "lastframe") == 0) *out = ViLayerStack_LastFrame;
    else return false;
    return true;
}
