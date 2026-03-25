/*
 * Touch-as-button input for Waveshare ESP32-S3-Touch-LCD-4.3
 * Uses LovyanGFX GT911 touch driver — a tap anywhere acts as a button press.
 * Replaces the physical button module from the original BambuHelper.
 */

#include "button.h"
#include "display_ui.h"
#include "settings.h"

static bool lastTouched = false;
static bool stableState = false;
static unsigned long lastChangeMs = 0;
static const unsigned long DEBOUNCE_MS = 100;  // slightly longer for touch

void initButton() {
  // Touch is initialized by LovyanGFX in initDisplay()
  // Nothing extra needed here
  lastTouched = false;
  stableState = false;
  lastChangeMs = 0;
}

bool wasButtonPressed() {
  // Read touch state from LovyanGFX
  lgfx::touch_point_t tp;
  bool raw = tft.getTouch(&tp);

  // Debounce
  if (raw != lastTouched) {
    lastChangeMs = millis();
    lastTouched = raw;
  }
  if ((millis() - lastChangeMs) < DEBOUNCE_MS) return false;

  // Rising edge detection (finger down)
  bool result = false;
  if (raw && !stableState) {
    result = true;
  }
  stableState = raw;

  return result;
}
