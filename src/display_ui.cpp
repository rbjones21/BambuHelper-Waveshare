#include "display_ui.h"
#include "display_gauges.h"
#include "display_anim.h"
#include "clock_mode.h"
#include "clock_pong.h"
#include "icons.h"
#include "config.h"
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "settings.h"
#include <WiFi.h>
#include <Wire.h>
#include <time.h>

LGFX_Waveshare43 tft;

// Use user-configured bg color instead of hardcoded CLR_BG
#undef  CLR_BG
#define CLR_BG  (dispSettings.bgColor)

static ScreenState currentScreen = SCREEN_SPLASH;
static ScreenState prevScreen = SCREEN_SPLASH;
static bool forceRedraw = true;
static unsigned long lastDisplayUpdate = 0;

// Previous state for smart redraw
static BambuState prevState;
static unsigned long connectScreenStart = 0;

// ---------------------------------------------------------------------------
//  Smooth gauge interpolation — values lerp toward MQTT actuals each frame
// ---------------------------------------------------------------------------
static float smoothNozzleTemp = 0;
static float smoothBedTemp    = 0;
static float smoothPartFan    = 0;
static float smoothAuxFan     = 0;
static float smoothChamberFan = 0;
static bool  smoothInited     = false;

static const float SMOOTH_ALPHA = 0.25f;
static const float SNAP_THRESH  = 0.5f;

static void smoothLerp(float& cur, float target) {
  float diff = target - cur;
  if (fabsf(diff) < SNAP_THRESH) cur = target;
  else cur += diff * SMOOTH_ALPHA;
}

static bool tickGaugeSmooth(const BambuState& s, bool snap) {
  if (snap || !smoothInited) {
    smoothNozzleTemp = s.nozzleTemp;
    smoothBedTemp    = s.bedTemp;
    smoothPartFan    = s.coolingFanPct;
    smoothAuxFan     = s.auxFanPct;
    smoothChamberFan = s.chamberFanPct;
    smoothInited = true;
    return false;
  }
  smoothLerp(smoothNozzleTemp, s.nozzleTemp);
  smoothLerp(smoothBedTemp,    s.bedTemp);
  smoothLerp(smoothPartFan,    (float)s.coolingFanPct);
  smoothLerp(smoothAuxFan,     (float)s.auxFanPct);
  smoothLerp(smoothChamberFan, (float)s.chamberFanPct);

  const float ANIM_EPS = 0.01f;
  return (fabsf(smoothNozzleTemp - s.nozzleTemp) > ANIM_EPS) ||
         (fabsf(smoothBedTemp    - s.bedTemp)    > ANIM_EPS) ||
         (fabsf(smoothPartFan    - (float)s.coolingFanPct) > ANIM_EPS) ||
         (fabsf(smoothAuxFan     - (float)s.auxFanPct)     > ANIM_EPS) ||
         (fabsf(smoothChamberFan - (float)s.chamberFanPct) > ANIM_EPS);
}

// ---------------------------------------------------------------------------
//  CH422G IO Expander (backlight, LCD reset, touch reset)
// ---------------------------------------------------------------------------
static uint8_t ch422gState = 0;

void ch422gInit() {
  Wire.begin(GPIO_NUM_8, GPIO_NUM_9, 400000);
  // Set all outputs: backlight ON, resets de-asserted (high)
  ch422gState = CH422G_LCD_BL | CH422G_LCD_RST | CH422G_TP_RST;
  ch422gWrite(ch422gState);
}

void ch422gWrite(uint8_t data) {
  ch422gState = data;
  Wire.beginTransmission(CH422G_WRITE_ADDR >> 1);
  Wire.write(data);
  Wire.endTransmission();
}

// ---------------------------------------------------------------------------
//  Backlight (on/off via CH422G — no PWM on this board)
// ---------------------------------------------------------------------------
void setBacklight(uint8_t level) {
  if (level > 0) {
    ch422gState |= CH422G_LCD_BL;
  } else {
    ch422gState &= ~CH422G_LCD_BL;
  }
  ch422gWrite(ch422gState);
}

// ---------------------------------------------------------------------------
//  Init
// ---------------------------------------------------------------------------
void initDisplay() {
  Serial.println("Display: CH422G init...");
  ch422gInit();

  // Reset LCD and touch via IO expander
  ch422gState &= ~(CH422G_LCD_RST | CH422G_TP_RST);
  ch422gWrite(ch422gState);
  delay(20);
  ch422gState |= CH422G_LCD_RST | CH422G_TP_RST;
  ch422gWrite(ch422gState);
  delay(100);

  Serial.println("Display: calling tft.init()...");
  tft.init();
  Serial.println("Display: tft.init() done");
  tft.setRotation(0);  // landscape
  tft.fillScreen(CLR_BG);
  Serial.println("Display: fillScreen done");

  setBacklight(200);  // just turns on

  memset(&prevState, 0, sizeof(prevState));

  // Splash screen
  tft.setTextSize(1);
  tft.setTextDatum(middle_center);
  tft.setTextColor(CLR_GREEN, CLR_BG);
  tft.setFont(FONT_TITLE);
  tft.drawString("BambuHelper", SCREEN_W / 2, SCREEN_H / 2 - 60);
  tft.setFont(FONT_MEDIUM);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("Printer Monitor", SCREEN_W / 2, SCREEN_H / 2 + 20);
  tft.setFont(FONT_BODY);
  tft.drawString(FW_VERSION, SCREEN_W / 2, SCREEN_H / 2 + 70);
}

void applyDisplaySettings() {
  tft.fillScreen(dispSettings.bgColor);
  forceRedraw = true;
}

void triggerDisplayTransition() {
  memset(&prevState, 0, sizeof(prevState));
  smoothInited = false;
  resetGaugeTextCache();
  tft.fillScreen(dispSettings.bgColor);
  forceRedraw = true;
}

void setScreenState(ScreenState state) {
  currentScreen = state;
}

ScreenState getScreenState() {
  return currentScreen;
}

// ---------------------------------------------------------------------------
//  Nozzle label helper (dual nozzle H2D/H2C)
// ---------------------------------------------------------------------------
static const char* nozzleLabel(const BambuState& s) {
  if (!s.dualNozzle) return "Nozzle";
  return s.activeNozzle == 0 ? "Nozzle R" : "Nozzle L";
}

// ---------------------------------------------------------------------------
//  Speed level name helper
// ---------------------------------------------------------------------------
static const char* speedLevelName(uint8_t level) {
  switch (level) {
    case 1: return "Silent";
    case 2: return "Standard";
    case 3: return "Sport";
    case 4: return "Ludicrous";
    default: return "---";
  }
}

static uint16_t speedLevelColor(uint8_t level) {
  switch (level) {
    case 1: return CLR_BLUE;
    case 2: return CLR_GREEN;
    case 3: return CLR_ORANGE;
    case 4: return CLR_RED;
    default: return CLR_TEXT_DIM;
  }
}

// ---------------------------------------------------------------------------
//  Screen: AP Mode
// ---------------------------------------------------------------------------
static void drawAPMode() {
  tft.setTextSize(1);
  tft.setTextDatum(middle_center);

  tft.setTextColor(CLR_GREEN, CLR_BG);
  tft.setFont(FONT_TITLE);
  tft.drawString("WiFi Setup", SCREEN_W / 2, 80);

  tft.setFont(FONT_BODY);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString("Connect to WiFi:", SCREEN_W / 2, 150);

  tft.setTextColor(CLR_CYAN, CLR_BG);
  tft.setFont(FONT_LARGE);
  char ssid[32];
  uint32_t mac = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  snprintf(ssid, sizeof(ssid), "%s%04X", WIFI_AP_PREFIX, mac);
  tft.drawString(ssid, SCREEN_W / 2, 210);

  tft.setFont(FONT_BODY);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("Password:", SCREEN_W / 2, 270);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString(WIFI_AP_PASSWORD, SCREEN_W / 2, 300);

  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("Then open:", SCREEN_W / 2, 350);
  tft.setTextColor(CLR_ORANGE, CLR_BG);
  tft.setFont(FONT_LARGE);
  tft.drawString("192.168.4.1", SCREEN_W / 2, 400);
}

// ---------------------------------------------------------------------------
//  Screen: Connecting WiFi
// ---------------------------------------------------------------------------
static void drawConnectingWiFi() {
  tft.setTextSize(1);
  tft.setTextDatum(middle_center);

  tft.setFont(FONT_MEDIUM);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString("Connecting to WiFi", SCREEN_W / 2, SCREEN_H / 2 - 40);

  int16_t tw = tft.textWidth("Connecting to WiFi");
  drawAnimDots(tft, SCREEN_W / 2 + tw / 2, SCREEN_H / 2 - 50, CLR_TEXT);

  const int16_t barW = 400;
  const int16_t barH = 12;
  drawSlideBar(tft, (SCREEN_W - barW) / 2, SCREEN_H / 2,
               barW, barH, CLR_BLUE, CLR_TRACK);
}

// ---------------------------------------------------------------------------
//  Screen: WiFi Connected (show IP)
// ---------------------------------------------------------------------------
static void drawWiFiConnected() {
  if (!forceRedraw) return;

  tft.setTextDatum(middle_center);

  int cx = SCREEN_W / 2;
  int cy = SCREEN_H / 2 - 60;
  tft.fillCircle(cx, cy, 40, CLR_GREEN);
  for (int i = -2; i <= 2; i++) {
    tft.drawLine(cx - 18, cy + i,     cx - 6, cy + 14 + i, CLR_BG);
    tft.drawLine(cx - 6,  cy + 14 + i, cx + 18, cy - 10 + i, CLR_BG);
  }

  tft.setTextSize(1);
  tft.setTextColor(CLR_GREEN, CLR_BG);
  tft.setFont(FONT_LARGE);
  tft.drawString("WiFi Connected", SCREEN_W / 2, SCREEN_H / 2 + 20);

  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.setFont(FONT_BODY);
  tft.drawString(WiFi.localIP().toString().c_str(), SCREEN_W / 2, SCREEN_H / 2 + 70);
}

// ---------------------------------------------------------------------------
//  Screen: Connecting MQTT
// ---------------------------------------------------------------------------
static void drawConnectingMQTT() {
  tft.setTextSize(1);
  tft.setTextDatum(middle_center);

  tft.setFont(FONT_MEDIUM);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString("Connecting to Printer", SCREEN_W / 2, SCREEN_H / 2 - 60);

  int16_t tw = tft.textWidth("Connecting to Printer");
  drawAnimDots(tft, SCREEN_W / 2 + tw / 2, SCREEN_H / 2 - 70, CLR_TEXT);
  tft.setTextDatum(middle_center);

  const int16_t barW = 400;
  const int16_t barH = 12;
  drawSlideBar(tft, (SCREEN_W - barW) / 2, SCREEN_H / 2 - 20,
               barW, barH, CLR_ORANGE, CLR_TRACK);

  PrinterSlot& p = displayedPrinter();
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.setFont(FONT_BODY);

  const char* modeStr = isCloudMode(p.config.mode) ? "Cloud" : "LAN";
  char infoBuf[40];
  if (isCloudMode(p.config.mode)) {
    snprintf(infoBuf, sizeof(infoBuf), "[%s] %s", modeStr,
             strlen(p.config.serial) > 0 ? p.config.serial : "no serial!");
  } else {
    snprintf(infoBuf, sizeof(infoBuf), "[%s] %s",  modeStr,
             strlen(p.config.ip) > 0 ? p.config.ip : "no IP!");
  }
  tft.drawString(infoBuf, SCREEN_W / 2, SCREEN_H / 2 + 20);

  if (connectScreenStart > 0) {
    unsigned long elapsed = (millis() - connectScreenStart) / 1000;
    char elBuf[16];
    snprintf(elBuf, sizeof(elBuf), "%lus", elapsed);
    tft.fillRect(SCREEN_W / 2 - 50, SCREEN_H / 2 + 40, 100, 24, CLR_BG);
    tft.drawString(elBuf, SCREEN_W / 2, SCREEN_H / 2 + 50);
  }

  const MqttDiag& d = getMqttDiag(rotState.displayIndex);
  if (d.attempts > 0) {
    tft.setFont(FONT_SMALL);
    tft.setTextDatum(middle_center);

    char buf[40];
    snprintf(buf, sizeof(buf), "Attempt: %u", d.attempts);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    tft.drawString(buf, SCREEN_W / 2, SCREEN_H / 2 + 80);

    if (d.lastRc != 0) {
      snprintf(buf, sizeof(buf), "Err: %s", mqttRcToString(d.lastRc));
      tft.setTextColor(CLR_RED, CLR_BG);
      tft.drawString(buf, SCREEN_W / 2, SCREEN_H / 2 + 110);
    }
  }
}

// ---------------------------------------------------------------------------
//  Screen: Idle (connected, not printing)
// ---------------------------------------------------------------------------
static void drawIdleNoPrinter() {
  if (!forceRedraw) return;

  tft.setTextDatum(middle_center);

  tft.setTextSize(1);
  tft.setTextColor(CLR_GREEN, CLR_BG);
  tft.setFont(FONT_TITLE);
  tft.drawString("BambuHelper", SCREEN_W / 2, 80);

  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.setFont(FONT_BODY);
  tft.drawString("WiFi Connected", SCREEN_W / 2, 160);

  tft.fillCircle(SCREEN_W / 2, 200, 8, CLR_GREEN);

  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.setFont(FONT_BODY);
  tft.drawString("No printer configured", SCREEN_W / 2, 260);
  tft.drawString("Open in browser:", SCREEN_W / 2, 310);

  tft.setTextColor(CLR_ORANGE, CLR_BG);
  tft.setFont(FONT_LARGE);
  tft.drawString(WiFi.localIP().toString().c_str(), SCREEN_W / 2, 380);
}

static bool wasNoPrinter = false;

static void drawIdle() {
  if (!isAnyPrinterConfigured()) {
    wasNoPrinter = true;
    drawIdleNoPrinter();
    return;
  }

  if (wasNoPrinter) {
    wasNoPrinter = false;
    tft.fillScreen(dispSettings.bgColor);
    memset(&prevState, 0, sizeof(prevState));
    forceRedraw = true;
  }

  PrinterSlot& p = displayedPrinter();
  BambuState& s = p.state;

  bool animating = tickGaugeSmooth(s, forceRedraw);
  bool stateChanged = forceRedraw || (strcmp(s.gcodeState, prevState.gcodeState) != 0);
  bool tempChanged = forceRedraw || animating ||
                     (s.nozzleTemp != prevState.nozzleTemp) ||
                     (s.nozzleTarget != prevState.nozzleTarget) ||
                     (s.bedTemp != prevState.bedTemp) ||
                     (s.bedTarget != prevState.bedTarget);
  bool connChanged = forceRedraw || (s.connected != prevState.connected);
  bool wifiChanged = forceRedraw || (s.wifiSignal != prevState.wifiSignal);

  tft.setTextDatum(middle_center);

  // Printer name
  if (forceRedraw) {
    tft.setTextSize(1);
    tft.setTextColor(CLR_GREEN, CLR_BG);
    tft.setFont(FONT_LARGE);
    const char* name = (p.config.name[0] != '\0') ? p.config.name : "Bambu P1S";
    tft.drawString(name, SCREEN_W / 2, 60);
  }

  // Status badge
  if (stateChanged) {
    tft.setFont(FONT_BODY);
    uint16_t stateColor = CLR_TEXT_DIM;
    const char* stateStr = s.gcodeState;
    if (strcmp(s.gcodeState, "IDLE") == 0) {
      stateColor = CLR_GREEN; stateStr = "Ready";
    } else if (strcmp(s.gcodeState, "FAILED") == 0) {
      stateColor = CLR_RED; stateStr = "ERROR";
    } else if (strcmp(s.gcodeState, "UNKNOWN") == 0 || s.gcodeState[0] == '\0') {
      stateStr = "Waiting...";
    }
    tft.fillRect(0, 100, SCREEN_W, 40, CLR_BG);
    tft.setTextColor(stateColor, CLR_BG);
    tft.drawString(stateStr, SCREEN_W / 2, 120);
  }

  // Connected indicator
  if (connChanged) {
    tft.fillCircle(SCREEN_W / 2, 160, 8, s.connected ? CLR_GREEN : CLR_RED);
  }

  // Temperature gauges (two centered)
  if (tempChanged) {
    drawTempGauge(tft, SCREEN_W / 2 - 120, 260, GAUGE_RADIUS_SMALL,
                  s.nozzleTemp, s.nozzleTarget, 300.0f,
                  dispSettings.nozzle.arc, nozzleLabel(s), nullptr, forceRedraw,
                  &dispSettings.nozzle, smoothNozzleTemp);

    drawTempGauge(tft, SCREEN_W / 2 + 120, 260, GAUGE_RADIUS_SMALL,
                  s.bedTemp, s.bedTarget, 120.0f,
                  dispSettings.bed.arc, "Bed", nullptr, forceRedraw,
                  &dispSettings.bed, smoothBedTemp);
  }

  // Bottom: filament indicator or WiFi signal
  bool bottomChanged = wifiChanged || (s.ams.activeTray != prevState.ams.activeTray);
  if (bottomChanged) {
    tft.fillRect(0, SCREEN_H - 40, SCREEN_W, 40, CLR_BG);
    tft.setFont(FONT_SMALL);
    tft.setTextDatum(bottom_center);

    if (s.ams.present && s.ams.activeTray < AMS_MAX_TRAYS && s.ams.trays[s.ams.activeTray].present) {
      AmsTray& t = s.ams.trays[s.ams.activeTray];
      int cx = SCREEN_W / 2 - tft.textWidth(t.type) / 2 - 16;
      tft.drawCircle(cx, SCREEN_H - 18, 8, CLR_TEXT_DARK);
      tft.fillCircle(cx, SCREEN_H - 18, 7, t.colorRgb565);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(t.type, SCREEN_W / 2 + 6, SCREEN_H - 4);
    } else {
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      char wifiBuf[24];
      snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: %d dBm", s.wifiSignal);
      tft.drawString(wifiBuf, SCREEN_W / 2, SCREEN_H - 4);
    }
  }
}

// ---------------------------------------------------------------------------
//  Helper: draw WiFi signal indicator in bottom-left corner
// ---------------------------------------------------------------------------
static void drawWifiSignalIndicator(const BambuState& s) {
  tft.setTextDatum(middle_left);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.setFont(FONT_SMALL);
  char wifiBuf[16];
  snprintf(wifiBuf, sizeof(wifiBuf), "%ddBm", s.wifiSignal);
  tft.drawString(wifiBuf, 8, BOTTOM_BAR_Y + 10);
}

// ---------------------------------------------------------------------------
//  Screen: Printing (main dashboard)
//  Layout: LED bar | header | 2x3 gauge grid | info line | bottom bar
// ---------------------------------------------------------------------------
static void drawPrinting() {
  PrinterSlot& p = displayedPrinter();
  BambuState& s = p.state;

  bool animating = tickGaugeSmooth(s, forceRedraw);
  bool progChanged = forceRedraw || (s.progress != prevState.progress);
  bool tempChanged = forceRedraw || animating ||
                     (s.nozzleTemp != prevState.nozzleTemp) ||
                     (s.nozzleTarget != prevState.nozzleTarget) ||
                     (s.bedTemp != prevState.bedTemp) ||
                     (s.bedTarget != prevState.bedTarget);
  bool etaChanged = forceRedraw ||
                     (s.remainingMinutes != prevState.remainingMinutes);
  bool fansChanged = forceRedraw || animating ||
                     (s.coolingFanPct != prevState.coolingFanPct) ||
                     (s.auxFanPct != prevState.auxFanPct) ||
                     (s.chamberFanPct != prevState.chamberFanPct);
  bool stateChanged = forceRedraw ||
                      (strcmp(s.gcodeState, prevState.gcodeState) != 0);

  const int16_t gR = GAUGE_RADIUS_LARGE;
  const int16_t gT = GAUGE_THICKNESS;

  // === H2-style LED progress bar (top) ===
  if (progChanged) {
    drawLedProgressBar(tft, 0, s.progress);
  }

  // === Header bar ===
  if (forceRedraw || stateChanged) {
    uint16_t hdrBg = dispSettings.bgColor;
    tft.fillRect(0, HEADER_Y, SCREEN_W, HEADER_H, hdrBg);

    // Printer name (left)
    tft.setTextDatum(middle_left);
    tft.setFont(FONT_MEDIUM);
    tft.setTextColor(CLR_TEXT, hdrBg);
    const char* name = (p.config.name[0] != '\0') ? p.config.name : "Bambu P1S";
    tft.drawString(name, 12, HEADER_Y + HEADER_H / 2);

    // State badge (right)
    uint16_t badgeColor = CLR_TEXT_DIM;
    if (strcmp(s.gcodeState, "RUNNING") == 0) badgeColor = CLR_GREEN;
    else if (strcmp(s.gcodeState, "PAUSE") == 0) badgeColor = CLR_YELLOW;
    else if (strcmp(s.gcodeState, "FAILED") == 0) badgeColor = CLR_RED;
    else if (strcmp(s.gcodeState, "PREPARE") == 0) badgeColor = CLR_BLUE;

    tft.setTextDatum(middle_right);
    tft.setTextColor(badgeColor, hdrBg);
    tft.setFont(FONT_MEDIUM);
    tft.fillCircle(SCREEN_W - 12 - tft.textWidth(s.gcodeState) - 14, HEADER_Y + HEADER_H / 2, 8, badgeColor);
    tft.drawString(s.gcodeState, SCREEN_W - 12, HEADER_Y + HEADER_H / 2);

    // Printer indicator dots (multi-printer)
    if (getActiveConnCount() > 1) {
      for (uint8_t di = 0; di < MAX_ACTIVE_PRINTERS; di++) {
        if (!isPrinterConfigured(di)) continue;
        uint16_t dotClr = (di == rotState.displayIndex) ? CLR_GREEN : CLR_TEXT_DARK;
        tft.fillCircle(SCREEN_W / 2 - 8 + di * 16, HEADER_Y + 8, 5, dotClr);
      }
    }
  }

  // === Row 1: Progress | Nozzle | Bed ===
  if (progChanged || forceRedraw) {
    drawProgressArc(tft, GAUGE_COL1_X, GAUGE_ROW1_Y, gR, gT,
                    s.progress, prevState.progress,
                    s.remainingMinutes, forceRedraw);
  }

  if (tempChanged) {
    drawTempGauge(tft, GAUGE_COL2_X, GAUGE_ROW1_Y, gR,
                  s.nozzleTemp, s.nozzleTarget, 300.0f,
                  dispSettings.nozzle.arc, nozzleLabel(s), nullptr, forceRedraw,
                  &dispSettings.nozzle, smoothNozzleTemp);

    drawTempGauge(tft, GAUGE_COL3_X, GAUGE_ROW1_Y, gR,
                  s.bedTemp, s.bedTarget, 120.0f,
                  dispSettings.bed.arc, "Bed", nullptr, forceRedraw,
                  &dispSettings.bed, smoothBedTemp);
  }

  // === Row 2: Part Fan | Aux Fan | Chamber Fan ===
  if (fansChanged) {
    drawFanGauge(tft, GAUGE_COL1_X, GAUGE_ROW2_Y, gR,
                 s.coolingFanPct, dispSettings.partFan.arc, "Part Fan", forceRedraw,
                 &dispSettings.partFan, smoothPartFan);

    drawFanGauge(tft, GAUGE_COL2_X, GAUGE_ROW2_Y, gR,
                 s.auxFanPct, dispSettings.auxFan.arc, "Aux Fan", forceRedraw,
                 &dispSettings.auxFan, smoothAuxFan);

    drawFanGauge(tft, GAUGE_COL3_X, GAUGE_ROW2_Y, gR,
                 s.chamberFanPct, dispSettings.chamberFan.arc, "Chamber", forceRedraw,
                 &dispSettings.chamberFan, smoothChamberFan);
  }

  // === Info line — ETA / PAUSE / ERROR ===
  if (etaChanged || stateChanged) {
    tft.fillRect(0, INFO_LINE_Y - 20, SCREEN_W, 40, CLR_BG);
    tft.setTextDatum(middle_center);

    if (strcmp(s.gcodeState, "PAUSE") == 0) {
      tft.setFont(FONT_LARGE);
      tft.setTextColor(CLR_YELLOW, CLR_BG);
      tft.drawString("PAUSED", SCREEN_W / 2, INFO_LINE_Y);
    } else if (strcmp(s.gcodeState, "FAILED") == 0) {
      tft.setFont(FONT_LARGE);
      tft.setTextColor(CLR_RED, CLR_BG);
      tft.drawString("ERROR!", SCREEN_W / 2, INFO_LINE_Y);
    } else if (s.remainingMinutes > 0) {
      static bool ntpSynced = false;
      time_t nowEpoch = time(nullptr);
      struct tm now;
      localtime_r(&nowEpoch, &now);
      if (now.tm_year > (2020 - 1900)) ntpSynced = true;

      if (ntpSynced) {
        time_t etaEpoch = nowEpoch + (time_t)s.remainingMinutes * 60;
        struct tm etaTm;
        localtime_r(&etaEpoch, &etaTm);

        char etaBuf[32];
        int etaH = etaTm.tm_hour;
        const char* ampm = "";
        if (!netSettings.use24h) {
          ampm = etaH < 12 ? "AM" : "PM";
          etaH = etaH % 12;
          if (etaH == 0) etaH = 12;
        }
        if (etaTm.tm_yday != now.tm_yday || etaTm.tm_year != now.tm_year) {
          if (netSettings.use24h)
            snprintf(etaBuf, sizeof(etaBuf), "ETA: %d.%02d %02d:%02d",
                     etaTm.tm_mday, etaTm.tm_mon + 1, etaH, etaTm.tm_min);
          else
            snprintf(etaBuf, sizeof(etaBuf), "ETA: %d.%02d %d:%02d%s",
                     etaTm.tm_mday, etaTm.tm_mon + 1, etaH, etaTm.tm_min, ampm);
        } else {
          if (netSettings.use24h)
            snprintf(etaBuf, sizeof(etaBuf), "ETA: %02d:%02d", etaH, etaTm.tm_min);
          else
            snprintf(etaBuf, sizeof(etaBuf), "ETA: %d:%02d %s", etaH, etaTm.tm_min, ampm);
        }
        tft.setFont(FONT_BODY);
        tft.setTextColor(CLR_GREEN, CLR_BG);
        tft.drawString(etaBuf, SCREEN_W / 2, INFO_LINE_Y);
      } else {
        char remBuf[24];
        uint16_t h = s.remainingMinutes / 60;
        uint16_t m = s.remainingMinutes % 60;
        snprintf(remBuf, sizeof(remBuf), "Remaining: %dh %02dm", h, m);
        tft.setFont(FONT_BODY);
        tft.setTextColor(CLR_TEXT, CLR_BG);
        tft.drawString(remBuf, SCREEN_W / 2, INFO_LINE_Y);
      }
    } else {
      tft.setFont(FONT_BODY);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString("ETA: ---", SCREEN_W / 2, INFO_LINE_Y);
    }
  }

  // === Bottom status bar — Filament/WiFi | Layer | Speed ===
  bool showingWifi = !(s.ams.present && s.ams.activeTray < AMS_MAX_TRAYS && s.ams.trays[s.ams.activeTray].present)
                  && !(s.ams.vtPresent && s.ams.activeTray == 254);
  bool bottomChanged = forceRedraw ||
                       (s.speedLevel != prevState.speedLevel) ||
                       (s.layerNum != prevState.layerNum) ||
                       (s.totalLayers != prevState.totalLayers) ||
                       (s.ams.activeTray != prevState.ams.activeTray) ||
                       (showingWifi && s.wifiSignal != prevState.wifiSignal);
  if (bottomChanged) {
    tft.fillRect(0, BOTTOM_BAR_Y, SCREEN_W, 24, CLR_BG);
    tft.setFont(FONT_SMALL);

    // Left: filament indicator or WiFi signal
    if (s.ams.present && s.ams.activeTray < AMS_MAX_TRAYS) {
      AmsTray& t = s.ams.trays[s.ams.activeTray];
      if (t.present) {
        tft.drawCircle(16, BOTTOM_BAR_Y + 12, 7, CLR_TEXT_DARK);
        tft.fillCircle(16, BOTTOM_BAR_Y + 12, 6, t.colorRgb565);
        tft.setTextDatum(middle_left);
        tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
        tft.drawString(t.type, 28, BOTTOM_BAR_Y + 12);
      } else {
        drawWifiSignalIndicator(s);
      }
    } else if (s.ams.vtPresent && s.ams.activeTray == 254) {
      tft.drawCircle(16, BOTTOM_BAR_Y + 12, 7, CLR_TEXT_DARK);
      tft.fillCircle(16, BOTTOM_BAR_Y + 12, 6, s.ams.vtColorRgb565);
      tft.setTextDatum(middle_left);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(s.ams.vtType, 28, BOTTOM_BAR_Y + 12);
    } else {
      drawWifiSignalIndicator(s);
    }

    // Layer count (center)
    tft.setTextDatum(middle_center);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    char layerBuf[20];
    snprintf(layerBuf, sizeof(layerBuf), "L%d/%d", s.layerNum, s.totalLayers);
    tft.drawString(layerBuf, SCREEN_W / 2, BOTTOM_BAR_Y + 12);

    // Speed mode (right)
    tft.setTextDatum(middle_right);
    tft.setTextColor(speedLevelColor(s.speedLevel), CLR_BG);
    tft.drawString(speedLevelName(s.speedLevel), SCREEN_W - 12, BOTTOM_BAR_Y + 12);
  }
}

// ---------------------------------------------------------------------------
//  Screen: Finished
// ---------------------------------------------------------------------------
static void drawFinished() {
  PrinterSlot& p = displayedPrinter();
  BambuState& s = p.state;

  bool animating = tickGaugeSmooth(s, forceRedraw);
  bool tempChanged = forceRedraw || animating ||
                     (s.nozzleTemp != prevState.nozzleTemp) ||
                     (s.nozzleTarget != prevState.nozzleTarget) ||
                     (s.bedTemp != prevState.bedTemp) ||
                     (s.bedTarget != prevState.bedTarget);

  const int16_t gR = GAUGE_RADIUS_LARGE;
  const int16_t gaugeLeft  = SCREEN_W / 2 - 120;
  const int16_t gaugeRight = SCREEN_W / 2 + 120;
  const int16_t gaugeY = 160;

  // === LED progress bar at 100% ===
  if (forceRedraw) {
    drawLedProgressBar(tft, 0, 100);
  }

  // === Header bar ===
  if (forceRedraw) {
    uint16_t hdrBg = dispSettings.bgColor;
    tft.fillRect(0, HEADER_Y, SCREEN_W, HEADER_H, hdrBg);

    tft.setTextDatum(middle_left);
    tft.setFont(FONT_MEDIUM);
    tft.setTextColor(CLR_TEXT, hdrBg);
    const char* name = (p.config.name[0] != '\0') ? p.config.name : "Printer";
    tft.drawString(name, 12, HEADER_Y + HEADER_H / 2);

    tft.setTextDatum(middle_right);
    tft.setTextColor(CLR_GREEN, hdrBg);
    tft.setFont(FONT_MEDIUM);
    tft.fillCircle(SCREEN_W - 12 - tft.textWidth("FINISH") - 14, HEADER_Y + HEADER_H / 2, 8, CLR_GREEN);
    tft.drawString("FINISH", SCREEN_W - 12, HEADER_Y + HEADER_H / 2);

    if (getActiveConnCount() > 1) {
      for (uint8_t di = 0; di < MAX_ACTIVE_PRINTERS; di++) {
        if (!isPrinterConfigured(di)) continue;
        uint16_t dotClr = (di == rotState.displayIndex) ? CLR_GREEN : CLR_TEXT_DARK;
        tft.fillCircle(SCREEN_W / 2 - 8 + di * 16, HEADER_Y + 8, 5, dotClr);
      }
    }
  }

  // === Temp gauges ===
  if (tempChanged) {
    drawTempGauge(tft, gaugeLeft, gaugeY, gR,
                  s.nozzleTemp, s.nozzleTarget, 300.0f,
                  dispSettings.nozzle.arc, nozzleLabel(s), nullptr, forceRedraw,
                  &dispSettings.nozzle, smoothNozzleTemp);

    drawTempGauge(tft, gaugeRight, gaugeY, gR,
                  s.bedTemp, s.bedTarget, 120.0f,
                  dispSettings.bed.arc, "Bed", nullptr, forceRedraw,
                  &dispSettings.bed, smoothBedTemp);
  }

  // === "Print Complete!" ===
  if (forceRedraw) {
    tft.setTextDatum(middle_center);
    tft.setTextColor(CLR_GREEN, CLR_BG);
    tft.setFont(FONT_TITLE);
    tft.drawString("Print Complete!", SCREEN_W / 2, 300);
  }

  // === File name ===
  if (forceRedraw) {
    tft.setTextDatum(middle_center);
    tft.setFont(FONT_BODY);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    if (s.subtaskName[0] != '\0') {
      char truncName[40];
      strncpy(truncName, s.subtaskName, 39);
      truncName[39] = '\0';
      tft.drawString(truncName, SCREEN_W / 2, 350);
    }
  }

  // === Bottom: WiFi ===
  if (forceRedraw) {
    tft.fillRect(0, SCREEN_H - 40, SCREEN_W, 40, CLR_BG);
    tft.setFont(FONT_SMALL);
    tft.setTextDatum(middle_center);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    char wifiBuf[20];
    snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: %d dBm", s.wifiSignal);
    tft.drawString(wifiBuf, SCREEN_W / 2, SCREEN_H - 18);
  }
}

// ---------------------------------------------------------------------------
//  Main update (called from loop)
// ---------------------------------------------------------------------------
void updateDisplay() {
  // Shimmer runs at its own cadence (~40fps)
  if (currentScreen == SCREEN_PRINTING) {
    BambuState& sh = displayedPrinter().state;
    tickProgressShimmer(tft, 0, sh.progress, sh.printing);
  }
  // Pong clock runs at ~50fps
  if (currentScreen == SCREEN_CLOCK && dispSettings.pongClock) {
    tickPongClock();
  }

  unsigned long now = millis();
  if (now - lastDisplayUpdate < DISPLAY_UPDATE_MS) return;
  lastDisplayUpdate = now;

  // Detect screen change
  if (currentScreen != prevScreen) {
    if (prevScreen == SCREEN_OFF && currentScreen != SCREEN_OFF) {
      setBacklight(brightness);
    }
    tft.setTextSize(1);
    tft.fillScreen(currentScreen == SCREEN_OFF ? TFT_BLACK : dispSettings.bgColor);
    forceRedraw = true;
    if (currentScreen == SCREEN_CONNECTING_MQTT) {
      connectScreenStart = millis();
    }
    if (currentScreen == SCREEN_CLOCK) {
      if (dispSettings.pongClock) resetPongClock();
      else resetClock();
    }
    prevScreen = currentScreen;
  }

  switch (currentScreen) {
    case SCREEN_SPLASH:
      break;

    case SCREEN_AP_MODE:
      if (forceRedraw) drawAPMode();
      break;

    case SCREEN_CONNECTING_WIFI:
      drawConnectingWiFi();
      break;

    case SCREEN_WIFI_CONNECTED:
      drawWiFiConnected();
      break;

    case SCREEN_CONNECTING_MQTT:
      drawConnectingMQTT();
      break;

    case SCREEN_IDLE:
      drawIdle();
      break;

    case SCREEN_PRINTING:
      drawPrinting();
      break;

    case SCREEN_FINISHED:
      drawFinished();
      break;

    case SCREEN_CLOCK:
      if (!dispSettings.pongClock) drawClock();
      break;

    case SCREEN_OFF:
      if (forceRedraw) {
        tft.fillScreen(TFT_BLACK);
        setBacklight(0);
      }
      break;
  }

  memcpy(&prevState, &displayedPrinter().state, sizeof(BambuState));
  forceRedraw = false;
}
