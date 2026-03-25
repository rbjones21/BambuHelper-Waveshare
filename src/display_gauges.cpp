#include "display_gauges.h"
#include "config.h"
#include "settings.h"

// ---------------------------------------------------------------------------
//  H2-style LED progress bar
// ---------------------------------------------------------------------------
void drawLedProgressBar(LGFX_Waveshare43& tft, int16_t y, uint8_t progress) {
  uint16_t bg = dispSettings.bgColor;
  uint16_t track = dispSettings.trackColor;

  const int16_t barW = SCREEN_W - 20;
  const int16_t barH = LED_BAR_H;
  const int16_t barX = (SCREEN_W - barW) / 2;

  tft.fillRect(barX, y, barW, barH, bg);

  if (progress == 0) return;

  int16_t fillW = (progress * barW) / 100;
  if (fillW < 1) fillW = 1;

  uint16_t barColor = dispSettings.progress.arc;

  tft.fillRoundRect(barX, y, fillW, barH, 3, barColor);

  // Glow center line
  uint16_t glowColor = tft.color332(255, 255, 255);  // bright white approx
  tft.drawFastHLine(barX + 1, y + barH / 2, fillW - 2, glowColor);

  if (fillW > 6 && progress < 100) {
    tft.fillRect(barX + fillW - 4, y, 4, barH, glowColor);
  }

  if (fillW < barW) {
    tft.fillRoundRect(barX + fillW, y, barW - fillW, barH, 3, track);
  }
}

// ---------------------------------------------------------------------------
//  Shimmer animation for progress bar
// ---------------------------------------------------------------------------
static int16_t shimmerPos = -1;
static unsigned long shimmerLastMs = 0;
static bool shimmerPaused = false;
static unsigned long shimmerPauseStart = 0;

static const int16_t SHIMMER_W = 24;
static const uint16_t SHIMMER_INTERVAL = 25;
static const uint16_t SHIMMER_PAUSE = 1200;
static const int16_t SHIMMER_STEP = 5;

void tickProgressShimmer(LGFX_Waveshare43& tft, int16_t y, uint8_t progress, bool printing) {
  if (!dispSettings.animatedBar || !printing || progress == 0) return;

  unsigned long now = millis();

  if (shimmerPaused) {
    if (now - shimmerPauseStart < SHIMMER_PAUSE) return;
    shimmerPaused = false;
    shimmerPos = 0;
  }

  if (now - shimmerLastMs < SHIMMER_INTERVAL) return;
  shimmerLastMs = now;

  const int16_t barW = SCREEN_W - 20;
  const int16_t barH = LED_BAR_H;
  const int16_t barX = (SCREEN_W - barW) / 2;
  int16_t fillW = (progress * barW) / 100;
  if (fillW < SHIMMER_W + 6) return;

  uint16_t barColor = dispSettings.progress.arc;

  // Erase previous shimmer position
  if (shimmerPos > 0) {
    int16_t eraseX = barX + shimmerPos - SHIMMER_STEP;
    int16_t eraseW = SHIMMER_STEP;
    if (eraseX < barX) { eraseW -= (barX - eraseX); eraseX = barX; }
    if (eraseW > 0) {
      tft.fillRect(eraseX, y, eraseW, barH, barColor);
    }
  }

  // Draw shimmer highlight
  int16_t sx = barX + shimmerPos;
  int16_t sw = SHIMMER_W;
  if (sx + sw > barX + fillW) sw = barX + fillW - sx;
  if (sw > 0) {
    uint16_t bright = CLR_TEXT;
    uint16_t mid = CLR_TEXT_DIM;
    if (sw >= 4) {
      tft.fillRect(sx, y, 3, barH, mid);
      tft.fillRect(sx + 3, y, sw - 6 > 0 ? sw - 6 : 1, barH, bright);
      if (sw > 6) tft.fillRect(sx + sw - 3, y, 3, barH, mid);
    } else {
      tft.fillRect(sx, y, sw, barH, bright);
    }
  }

  shimmerPos += SHIMMER_STEP;

  if (shimmerPos >= fillW) {
    int16_t tailX = barX + fillW - SHIMMER_W - SHIMMER_STEP;
    if (tailX < barX) tailX = barX;
    tft.fillRect(tailX, y, barX + fillW - tailX, barH, barColor);
    uint16_t glowColor = CLR_TEXT;
    tft.drawFastHLine(barX + 1, y + barH / 2, fillW - 2, glowColor);

    shimmerPos = 0;
    shimmerPaused = true;
    shimmerPauseStart = now;
  }
}

// ---------------------------------------------------------------------------
//  Helper: draw arc track + fill
// ---------------------------------------------------------------------------
static void drawArcFill(LGFX_Waveshare43& tft, int16_t cx, int16_t cy,
                        int16_t radius, int16_t thickness,
                        uint16_t fillEnd, uint16_t fillColor, bool forceRedraw) {
  const uint16_t startAngle = 60;
  const uint16_t endAngle = 300;
  uint16_t bg = dispSettings.bgColor;
  uint16_t track = dispSettings.trackColor;

  if (forceRedraw) {
    tft.fillCircle(cx, cy, radius + 2, bg);
    tft.fillArc(cx, cy, radius, radius - thickness,
                startAngle, endAngle, track);
  }

  if (fillEnd > startAngle) {
    tft.fillArc(cx, cy, radius, radius - thickness,
                startAngle, fillEnd, fillColor);
  }

  if (fillEnd < endAngle) {
    tft.fillArc(cx, cy, radius, radius - thickness,
                fillEnd, endAngle, track);
  }
}

// ---------------------------------------------------------------------------
//  Helper: clear gauge center
// ---------------------------------------------------------------------------
static void clearGaugeCenter(LGFX_Waveshare43& tft, int16_t cx, int16_t cy,
                             int16_t radius, int16_t thickness) {
  int16_t textR = radius - thickness - 1;
  tft.fillCircle(cx, cy, textR, dispSettings.bgColor);
}

// ---------------------------------------------------------------------------
//  Text cache
// ---------------------------------------------------------------------------
#define GAUGE_CACHE_SLOTS 12

struct GaugeTextCache {
  int16_t cx, cy;
  char main[12];
  char sub[12];
};

static GaugeTextCache gCache[GAUGE_CACHE_SLOTS];
static uint8_t gCacheCount = 0;

static GaugeTextCache* gaugeCache(int16_t cx, int16_t cy) {
  for (uint8_t i = 0; i < gCacheCount; i++) {
    if (gCache[i].cx == cx && gCache[i].cy == cy) return &gCache[i];
  }
  if (gCacheCount < GAUGE_CACHE_SLOTS) {
    GaugeTextCache* c = &gCache[gCacheCount++];
    c->cx = cx; c->cy = cy;
    c->main[0] = '\0'; c->sub[0] = '\0';
    return c;
  }
  return nullptr;
}

static bool gaugeTextChanged(int16_t cx, int16_t cy, const char* main,
                             const char* sub, bool force) {
  if (force) {
    GaugeTextCache* c = gaugeCache(cx, cy);
    if (c) { strlcpy(c->main, main, sizeof(c->main)); strlcpy(c->sub, sub, sizeof(c->sub)); }
    return true;
  }
  GaugeTextCache* c = gaugeCache(cx, cy);
  if (!c) return true;
  bool changed = (strcmp(c->main, main) != 0) || (strcmp(c->sub, sub) != 0);
  if (changed) {
    strlcpy(c->main, main, sizeof(c->main));
    strlcpy(c->sub, sub, sizeof(c->sub));
  }
  return changed;
}

void resetGaugeTextCache() {
  gCacheCount = 0;
}

// ---------------------------------------------------------------------------
//  Main progress arc
// ---------------------------------------------------------------------------
void drawProgressArc(LGFX_Waveshare43& tft, int16_t cx, int16_t cy, int16_t radius,
                     int16_t thickness, uint8_t progress, uint8_t prevProgress,
                     uint16_t remainingMin, bool forceRedraw) {
  const uint16_t startAngle = 60;
  const GaugeColors& gc = dispSettings.progress;
  uint16_t bg = dispSettings.bgColor;

  uint16_t fillEnd = startAngle + (progress * 240) / 100;
  if (fillEnd > 300) fillEnd = 300;

  drawArcFill(tft, cx, cy, radius, thickness, fillEnd, gc.arc, forceRedraw);

  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", progress);
  char timeBuf[16];
  if (remainingMin >= 60) {
    snprintf(timeBuf, sizeof(timeBuf), "%dh%dm", remainingMin / 60, remainingMin % 60);
  } else {
    snprintf(timeBuf, sizeof(timeBuf), "%dm", remainingMin);
  }

  if (gaugeTextChanged(cx, cy, pctBuf, timeBuf, forceRedraw)) {
    clearGaugeCenter(tft, cx, cy, radius, thickness);

    tft.setTextSize(1);
    tft.setTextDatum(middle_center);
    tft.setTextColor(gc.value);
    tft.setFont(FONT_GAUGE_VAL);
    tft.drawString(pctBuf, cx, cy - 12);

    tft.setFont(FONT_GAUGE_SUB);
    tft.setTextColor(CLR_TEXT_DIM);
    tft.drawString(timeBuf, cx, cy + 24);

    tft.setFont(FONT_GAUGE_LBL);
    tft.setTextColor(gc.label, bg);
    tft.drawString("Progress", cx, cy + radius + 16);
  }
}

// ---------------------------------------------------------------------------
//  Temperature arc gauge
// ---------------------------------------------------------------------------
void drawTempGauge(LGFX_Waveshare43& tft, int16_t cx, int16_t cy, int16_t radius,
                   float current, float target, float maxTemp,
                   uint16_t accentColor, const char* label,
                   const uint8_t* icon, bool forceRedraw,
                   const GaugeColors* colors, float arcValue) {
  const uint16_t startAngle = 60;
  const int16_t thickness = GAUGE_THICKNESS;
  uint16_t bg = dispSettings.bgColor;

  uint16_t arcColor = colors ? colors->arc : accentColor;
  uint16_t lblColor = colors ? colors->label : accentColor;
  uint16_t valColor = colors ? colors->value : CLR_TEXT;

  float arcVal = (arcValue >= 0.0f) ? arcValue : current;
  float ratio = (maxTemp > 0) ? (arcVal / maxTemp) : 0;
  if (ratio > 1.0f) ratio = 1.0f;
  if (ratio < 0.0f) ratio = 0.0f;

  uint16_t fillEnd = startAngle + (uint16_t)(ratio * 240);
  if (fillEnd <= startAngle && ratio > 0.01f) fillEnd = startAngle + 1;
  if (fillEnd > 300) fillEnd = 300;

  uint16_t tempColor = arcColor;
  uint16_t drawFill = (ratio > 0.01f) ? fillEnd : startAngle;
  drawArcFill(tft, cx, cy, radius, thickness, drawFill, tempColor, forceRedraw);

  char tempBuf[12], targetBuf[12];
  snprintf(tempBuf, sizeof(tempBuf), "%.0f", current);
  snprintf(targetBuf, sizeof(targetBuf), "/%.0f", target);

  if (gaugeTextChanged(cx, cy, tempBuf, targetBuf, forceRedraw)) {
    clearGaugeCenter(tft, cx, cy, radius, thickness);

    tft.setTextSize(1);
    tft.setTextDatum(middle_center);
    tft.setFont(FONT_GAUGE_VAL);
    tft.setTextColor(valColor);
    tft.drawString(tempBuf, cx, cy - 8);

    tft.setFont(FONT_GAUGE_SUB);
    tft.setTextColor(CLR_TEXT_DIM);
    tft.drawString(targetBuf, cx, cy + 20);

    tft.setFont(FONT_GAUGE_LBL);
    tft.setTextColor(lblColor, bg);
    tft.drawString(label, cx, cy + radius + 16);
  }
}

// ---------------------------------------------------------------------------
//  Fan speed gauge (0-100%)
// ---------------------------------------------------------------------------
void drawFanGauge(LGFX_Waveshare43& tft, int16_t cx, int16_t cy, int16_t radius,
                  uint8_t percent, uint16_t accentColor, const char* label,
                  bool forceRedraw, const GaugeColors* colors,
                  float arcPercent) {
  const uint16_t startAngle = 60;
  const int16_t thickness = GAUGE_THICKNESS;
  uint16_t bg = dispSettings.bgColor;

  uint16_t arcColor = colors ? colors->arc : accentColor;
  uint16_t lblColor = colors ? colors->label : accentColor;
  uint16_t valColor = colors ? colors->value : CLR_TEXT;

  float arcVal = (arcPercent >= 0.0f) ? arcPercent : (float)percent;
  uint16_t fillEnd = startAngle + (uint16_t)(arcVal * 240.0f / 100.0f);
  if (fillEnd > 300) fillEnd = 300;

  uint16_t fanColor;
  if (percent == 0 && arcVal < 0.5f) {
    fanColor = CLR_TEXT_DIM;
  } else {
    fanColor = arcColor;
  }

  uint16_t drawFill = (arcVal > 0.5f) ? fillEnd : startAngle;
  drawArcFill(tft, cx, cy, radius, thickness, drawFill, fanColor, forceRedraw);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", percent);

  if (gaugeTextChanged(cx, cy, buf, "", forceRedraw)) {
    clearGaugeCenter(tft, cx, cy, radius, thickness);

    tft.setTextSize(1);
    tft.setTextDatum(middle_center);
    tft.setFont(FONT_GAUGE_VAL);
    tft.setTextColor(valColor);
    tft.drawString(buf, cx, cy);

    tft.setFont(FONT_GAUGE_LBL);
    tft.setTextColor(lblColor, bg);
    tft.drawString(label, cx, cy + radius + 16);
  }
}
