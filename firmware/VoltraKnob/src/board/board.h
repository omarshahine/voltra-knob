#pragma once
#include <cstdint>

namespace board {

struct RotateEvent { int8_t direction; uint32_t ms; };   // +1 clockwise, -1 counterclockwise

bool begin();                       // expander, display, LVGL display driver, touch, encoder task
bool pollRotate(RotateEvent& out);  // non-blocking; drains the encoder queue one event at a time
bool buttonPressed();               // PCF8574 P5, active low; true while pressed
void setBacklight(uint8_t level);   // 0..255

struct DebugStats { uint32_t encLoops, encEdges, levelA, levelB, encStackFree, queueWaiting; };
DebugStats debugStats();           // encoder task health counters
void dumpEncoderPins();            // print GPIO config of the encoder pins

}  // namespace board
