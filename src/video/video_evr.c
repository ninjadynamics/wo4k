#include "video/video_evr.h"

const GUID video_iid_display_control = {
    0xa490b1e4, 0xab84, 0x4d31, { 0xa1, 0xb2, 0x18, 0x1e, 0x03, 0xb1, 0x07, 0x7a }
};

const GUID video_clsid_evr = {
    0xfa10746c, 0x9b63, 0x4b6c, { 0xbc, 0x49, 0xfc, 0x30, 0x0e, 0xa5, 0xf2, 0x56 }
};

void video_evr_prepare(IMFVideoDisplayControl *display)
{
    IMFVideoDisplayControl_SetAspectRatioMode(display,
        MFVideoARMode_PreservePicture);
    IMFVideoDisplayControl_SetBorderColor(display, RGB(0, 0, 0));
}

void video_evr_layout(IMFVideoDisplayControl *display, const RECT *client)
{
    RECT destination = *client;
    IMFVideoDisplayControl_SetVideoPosition(display, NULL, &destination);
}
