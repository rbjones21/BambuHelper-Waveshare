#include "clock_mode.h"
#include "display_ui.h"
#include "settings.h"
#include "config.h"
#include <time.h>

static int prevMinute = -1;

void resetClock() {
  prevMinute = -1;
}

void drawClock() {
  struct tm now;
  if (!getLocalTime(&now, 0)) return;

  if (now.tm_min == prevMinute) return;
  prevMinute = now.tm_min;

  uint16_t bg = dispSettings.bgColor;

  // Clear clock area
  tft.fillRect(0, 100, SCREEN_W, 280, bg);

  // Time — large font
  char timeBuf[12];
  if (netSettings.use24h) {
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", now.tm_hour, now.tm_min);
  } else {
    int h = now.tm_hour % 12;
    if (h == 0) h = 12;
    snprintf(timeBuf, sizeof(timeBuf), "%2d:%02d", h, now.tm_min);
  }
  tft.setTextDatum(middle_center);
  tft.setFont(&fonts::Font7);
  tft.setTextSize(2);
  tft.setTextColor(CLR_TEXT, bg);
  tft.drawString(timeBuf, SCREEN_W / 2, 200);
  // AM/PM indicator for 12h mode
  if (!netSettings.use24h) {
    tft.setFont(FONT_LARGE);
    tft.setTextSize(1);
    tft.setTextColor(CLR_TEXT_DIM, bg);
    tft.drawString(now.tm_hour < 12 ? "AM" : "PM", SCREEN_W / 2, 280);
  }

  // Date
  const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  char dateBuf[20];
  snprintf(dateBuf, sizeof(dateBuf), "%s  %02d.%02d.%04d",
           days[now.tm_wday], now.tm_mday, now.tm_mon + 1, now.tm_year + 1900);
  tft.setFont(FONT_BODY);
  tft.setTextSize(1);
  tft.setTextColor(CLR_TEXT_DIM, bg);
  tft.drawString(dateBuf, SCREEN_W / 2, 330);
}
