#pragma once
#include <cstdint>

namespace board {

struct RotateEvent { int8_t direction; uint32_t ms; };   // +1 clockwise, -1 counterclockwise

bool begin();                       // expander, display, LVGL display driver, touch, encoder task
bool pollRotate(RotateEvent& out);  // non-blocking; drains the encoder queue one event at a time
bool buttonPressed();               // true while pressed (CrowPanel: PCF8574 P5; Waveshare: finger on the screen)
void setBacklight(uint8_t level);   // 0..255

enum class Haptic : uint8_t { Tick, Click, Double };
void haptic(Haptic kind);           // no-op on boards without a haptic motor

struct DebugStats { uint32_t encLoops, encEdges, levelA, levelB, encStackFree, queueWaiting; int arduinoA, arduinoB, rawA, rawB; uint32_t flushes, flushPixels, flushMaxUs; uint32_t edgesA, edgesB; };
DebugStats debugStats();           // encoder task health counters
void dumpEncoderPins();            // print GPIO config of the encoder pins

}  // namespace board
