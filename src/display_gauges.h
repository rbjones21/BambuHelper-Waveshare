#ifndef DISPLAY_GAUGES_H
#define DISPLAY_GAUGES_H

#include "lgfx_waveshare.h"

struct GaugeColors;  // forward declaration from settings.h

// Draw H2-style LED progress bar (full-width, top of screen)
void drawLedProgressBar(LGFX_Waveshare43& tft, int16_t y, uint8_t progress);

// Shimmer animation tick — call from loop(), runs at its own cadence
void tickProgressShimmer(LGFX_Waveshare43& tft, int16_t y, uint8_t progress, bool printing);

// Draw progress arc with percentage and time in center
void drawProgressArc(LGFX_Waveshare43& tft, int16_t cx, int16_t cy, int16_t radius,
                     int16_t thickness, uint8_t progress, uint8_t prevProgress,
                     uint16_t remainingMin, bool forceRedraw);

// Draw temperature arc gauge with current/target
void drawTempGauge(LGFX_Waveshare43& tft, int16_t cx, int16_t cy, int16_t radius,
                   float current, float target, float maxTemp,
                   uint16_t accentColor, const char* label,
                   const uint8_t* icon, bool forceRedraw,
                   const GaugeColors* colors = nullptr,
                   float arcValue = -1.0f);

// Draw fan speed gauge (0-100%)
void drawFanGauge(LGFX_Waveshare43& tft, int16_t cx, int16_t cy, int16_t radius,
                  uint8_t percent, uint16_t accentColor, const char* label,
                  bool forceRedraw, const GaugeColors* colors = nullptr,
                  float arcPercent = -1.0f);

// Reset cached text (call on screen/printer transitions)
void resetGaugeTextCache();

#endif // DISPLAY_GAUGES_H
