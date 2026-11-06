/* raylib build configuration for Setup.exe, force-included when compiling
 * raylib (-DEXTERNAL_CONFIG_FLAGS -include). Starts from raylib's own
 * config.h and removes everything Setup does not use: no audio, 3D models,
 * gestures, input recording, screenshot/GIF hotkeys, image export or extra
 * image formats. Only PNG artwork and TrueType fonts are loaded. */
#ifndef WO4K_RAYLIB_CONFIG_H
#define WO4K_RAYLIB_CONFIG_H

#include "config.h"

#undef SUPPORT_MODULE_RMODELS
#undef SUPPORT_MODULE_RAUDIO
#undef SUPPORT_CAMERA_SYSTEM
#undef SUPPORT_GESTURES_SYSTEM
#undef SUPPORT_MOUSE_GESTURES
#undef SUPPORT_SSH_KEYBOARD_RPI
/* Plain Sleep-based frame pacing: no busy waiting, no system timer change. */
#undef SUPPORT_WINMM_HIGHRES_TIMER
#undef SUPPORT_BUSY_WAIT_LOOP
#undef SUPPORT_PARTIALBUSY_WAIT_LOOP
/* raylib binds F12 to screenshots and Ctrl+F12 to GIF recording by default. */
#undef SUPPORT_SCREEN_CAPTURE
#undef SUPPORT_GIF_RECORDING
#undef SUPPORT_COMPRESSION_API
#undef SUPPORT_AUTOMATION_EVENTS
#undef SUPPORT_CLIPBOARD_IMAGE
#undef SUPPORT_IMAGE_EXPORT
#undef SUPPORT_FILEFORMAT_BMP
#undef SUPPORT_FILEFORMAT_TGA
#undef SUPPORT_FILEFORMAT_JPG
#undef SUPPORT_FILEFORMAT_GIF
#undef SUPPORT_FILEFORMAT_QOI
#undef SUPPORT_FILEFORMAT_PSD
#undef SUPPORT_FILEFORMAT_DDS
#undef SUPPORT_FILEFORMAT_HDR
#undef SUPPORT_FILEFORMAT_FNT
#undef SUPPORT_FILEFORMAT_BDF

#endif
