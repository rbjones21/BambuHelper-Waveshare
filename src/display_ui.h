#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include "lgfx_waveshare.h"

enum ScreenState {
  SCREEN_SPLASH,
  SCREEN_AP_MODE,
  SCREEN_CONNECTING_WIFI,
  SCREEN_WIFI_CONNECTED,
  SCREEN_CONNECTING_MQTT,
  SCREEN_IDLE,
  SCREEN_PRINTING,
  SCREEN_FINISHED,
  SCREEN_CLOCK,
  SCREEN_OFF
};

extern LGFX_Waveshare43 tft;

void initDisplay();
void updateDisplay();
void setScreenState(ScreenState state);
ScreenState getScreenState();
void setBacklight(uint8_t level);
void applyDisplaySettings();  // re-apply rotation, bg, force redraw
void triggerDisplayTransition(); // start printer-name overlay on rotation

// CH422G IO expander control
void ch422gInit();
void ch422gWrite(uint8_t data);

#endif // DISPLAY_UI_H
