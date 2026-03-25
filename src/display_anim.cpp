#include "display_anim.h"
#include "config.h"
#include "settings.h"
#include "icons.h"

// Use user-configured bg color instead of hardcoded CLR_BG
#undef  CLR_BG
#define CLR_BG  (dispSettings.bgColor)

// ---------------------------------------------------------------------------
//  Rotating arc spinner
// ---------------------------------------------------------------------------
static uint16_t spinnerAngle = 0;

void drawSpinner(LGFX_Waveshare43& tft, int16_t cx, int16_t cy, int16_t radius,
                 uint16_t color) {
  uint16_t prevStart = (spinnerAngle + 360 - 12) % 360;
  uint16_t prevEnd = (prevStart + 60) % 360;
  if (prevEnd > prevStart) {
    tft.fillArc(cx, cy, radius, radius - 4, prevStart, prevEnd, CLR_BG);
  } else {
    tft.fillArc(cx, cy, radius, radius - 4, prevStart, 360, CLR_BG);
    tft.fillArc(cx, cy, radius, radius - 4, 0, prevEnd, CLR_BG);
  }

  spinnerAngle = (spinnerAngle + 12) % 360;
  uint16_t arcStart = spinnerAngle;
  uint16_t arcEnd = (spinnerAngle + 60) % 360;

  if (arcEnd > arcStart) {
    tft.fillArc(cx, cy, radius, radius - 4, arcStart, arcEnd, color);
  } else {
    tft.fillArc(cx, cy, radius, radius - 4, arcStart, 360, color);
    tft.fillArc(cx, cy, radius, radius - 4, 0, arcEnd, color);
  }
}

// ---------------------------------------------------------------------------
//  Animated dots "..."
// ---------------------------------------------------------------------------
void drawAnimDots(LGFX_Waveshare43& tft, int16_t x, int16_t y, uint16_t color) {
  unsigned long ms = millis();
  int phase = (ms / 400) % 4;

  tft.setFont(FONT_BODY);
  tft.setTextSize(1);
  tft.setTextDatum(top_left);

  for (int i = 0; i < 3; i++) {
    uint16_t dotColor = (i < phase) ? color : CLR_TEXT_DARK;
    tft.setTextColor(dotColor, CLR_BG);
    tft.drawString(".", x + i * 14, y);
  }
}

// ---------------------------------------------------------------------------
//  Indeterminate slide bar
// ---------------------------------------------------------------------------
void drawSlideBar(LGFX_Waveshare43& tft, int16_t x, int16_t y, int16_t w, int16_t h,
                  uint16_t color, uint16_t trackColor) {
  tft.fillRoundRect(x, y, w, h, h / 2, trackColor);

  const int16_t segW = w / 4;
  float t = (millis() % 1600) / 1600.0f;
  float pos = (sinf(t * 2.0f * PI - PI / 2.0f) + 1.0f) / 2.0f;
  int16_t segX = x + (int16_t)(pos * (float)(w - segW));

  tft.fillRoundRect(segX, y, segW, h, h / 2, color);
}

// ---------------------------------------------------------------------------
//  Pulse factor for glow effects
// ---------------------------------------------------------------------------
float getPulseFactor() {
  unsigned long ms = millis();
  float t = (ms % 2000) / 2000.0f;
  float sine = sinf(t * 2.0f * PI);
  return 0.75f + 0.25f * sine;
}

// ---------------------------------------------------------------------------
//  Completion animation
// ---------------------------------------------------------------------------
static unsigned long completionStart = 0;
static bool completionDone = false;
static int16_t prevRing = 0;

void drawCompletionAnim(LGFX_Waveshare43& tft, int16_t cx, int16_t cy, bool reset) {
  if (reset) {
    completionStart = millis();
    completionDone = false;
    prevRing = 0;
    return;
  }

  if (completionDone) return;

  unsigned long elapsed = millis() - completionStart;
  const int16_t finalR = 70;

  if (elapsed < 400) {
    int16_t r = 15 + (elapsed * (finalR - 15)) / 400;
    if (prevRing > 0 && prevRing != r) {
      tft.fillArc(cx, cy, prevRing, prevRing - 4, 0, 360, CLR_BG);
    }
    tft.fillArc(cx, cy, r, r - 4, 0, 360, CLR_GREEN);
    prevRing = r;
  } else if (elapsed < 600) {
    tft.fillArc(cx, cy, finalR, finalR - 5, 0, 360, CLR_GREEN);
  } else {
    tft.fillArc(cx, cy, finalR, finalR - 5, 0, 360, CLR_GREEN);
    tft.fillCircle(cx, cy, finalR - 6, CLR_BG);
    drawIcon32(tft, cx - 16, cy - 16, icon_check_32, CLR_GREEN);
    completionDone = true;
  }
}
