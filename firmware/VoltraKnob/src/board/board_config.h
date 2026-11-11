#pragma once
// Compile-time board selection. tools/build.sh passes -DVOLTRA_BOARD_WAVESHARE_KNOB18=1 when
// BOARD=waveshare; the default build targets the Elecrow CrowPanel 2.1".
//
//   CrowPanel 2.1"  : ESP32-S3, 480x480 ST7701 parallel RGB, push button on a PCF8574.
//   Waveshare 1.8"  : ESP32-S3-Knob-Touch-LCD-1.8, 360x360 ST77916 QSPI, CST816 touch,
//                     DRV2605 haptics, no push button (the touchscreen is the button).

#ifndef VOLTRA_BOARD_WAVESHARE_KNOB18
#define VOLTRA_BOARD_WAVESHARE_KNOB18 0
#endif

#if VOLTRA_BOARD_WAVESHARE_KNOB18
#define BOARD_NAME "waveshare-knob-1.8"
#define BOARD_SCREEN_PX 360
#else
#define BOARD_NAME "crowpanel-2.1"
#define BOARD_SCREEN_PX 480
#endif
