#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
//  Firmware version
// =============================================================================
#define FW_VERSION          "v2.3-ws43"

// =============================================================================
//  Display (Waveshare ESP32-S3-Touch-LCD-4.3: 800x480 RGB)
// =============================================================================
#define SCREEN_W        800
#define SCREEN_H        480

// Backlight is controlled via CH422G IO expander (on/off only, no PWM)
// See lgfx_waveshare.h for pin definitions

// =============================================================================
//  Color palette (RGB565)
// =============================================================================
#define CLR_BG          0x0000   // black
#define CLR_CARD        0x1926   // dark card bg
#define CLR_HEADER      0x10A2   // header bar bg
#define CLR_TEXT         0xFFFF   // white
#define CLR_TEXT_DIM     0xC618   // gray text
#define CLR_TEXT_DARK    0x7BEF   // darker gray
#define CLR_GREEN        0x07E0   // bright green
#define CLR_GREEN_DARK   0x0400   // dark green (track)
#define CLR_BLUE         0x34DF   // accent blue
#define CLR_ORANGE       0xFBE0   // nozzle accent
#define CLR_CYAN         0x07FF   // bed accent
#define CLR_RED          0xF800   // error / hot
#define CLR_YELLOW       0xFFE0   // pause / warm
#define CLR_GOLD         0xFEA0   // progress near done
#define CLR_TRACK        0x18E3   // arc background track

// =============================================================================
//  MQTT / Bambu
// =============================================================================
#define BAMBU_PORT                  8883
#define BAMBU_USERNAME              "bblp"
#define BAMBU_BUFFER_SIZE           16384   // 16KB for full pushall
#define BAMBU_RECONNECT_INTERVAL    10000   // 10s between attempts
#define BAMBU_BACKOFF_PHASE1        5       // first N attempts at normal interval
#define BAMBU_BACKOFF_PHASE2_MS     60000   // 60s after phase 1 exhausted
#define BAMBU_BACKOFF_PHASE2        10      // next N attempts at phase 2 interval
#define BAMBU_BACKOFF_PHASE3_MS     120000  // 120s after phase 2 exhausted
#define BAMBU_STALE_TIMEOUT         60000   // 60s no data = stale
#define BAMBU_PUSHALL_INTERVAL      30000   // request full status every 30s
#define BAMBU_PUSHALL_INITIAL_DELAY 2000    // wait 2s after connect
#define BAMBU_MIN_FREE_HEAP         40000   // min heap for TLS allocation
#define BAMBU_KEEPALIVE             60

// =============================================================================
//  WiFi
// =============================================================================
#define WIFI_AP_PREFIX      "BambuHelper-"
#define WIFI_AP_PASSWORD    "bambu1234"
#define WIFI_CONNECT_TIMEOUT 15000  // 15s STA connect timeout
#define WIFI_RECONNECT_TIMEOUT 60000 // 60s before re-entering AP mode

// =============================================================================
//  NVS
// =============================================================================
#define NVS_NAMESPACE       "bambu"

// =============================================================================
//  Multi-printer
// =============================================================================
#define MAX_PRINTERS          4       // NVS config slots
#define MAX_ACTIVE_PRINTERS   2       // max simultaneous MQTT connections

// =============================================================================
//  Display rotation (multi-printer)
// =============================================================================
#define ROTATE_INTERVAL_MS    60000   // default auto-rotate: 1 min
#define ROTATE_MIN_MS         10000   // min allowed interval (10s)
#define ROTATE_MAX_MS         600000  // max allowed interval (10 min)

// =============================================================================
//  Touch input (GT911 capacitive touch via LovyanGFX)
// =============================================================================
// Touch is handled by LovyanGFX GT911 driver, no separate GPIO needed
// Button compatibility: touch tap acts as button press
#define BUTTON_DEFAULT_PIN    0       // not used for touch — kept for settings compat

// =============================================================================
//  Display refresh
// =============================================================================
#define DISPLAY_UPDATE_MS   250   // ~4 Hz refresh rate

// =============================================================================
//  Buzzer (optional passive buzzer)
// =============================================================================
#define BUZZER_DEFAULT_PIN    5       // default GPIO for buzzer

// =============================================================================
//  Layout constants for 800x480 display
// =============================================================================

// Gauge sizes
#define GAUGE_RADIUS_LARGE   58     // main gauges on printing screen
#define GAUGE_RADIUS_SMALL   50     // idle/finish screen gauges
#define GAUGE_THICKNESS      10     // arc thickness

// Printing screen 2x3 gauge grid
#define GAUGE_COL1_X    133         // left column center
#define GAUGE_COL2_X    400         // middle column center
#define GAUGE_COL3_X    667         // right column center
#define GAUGE_ROW1_Y    140         // row 1 center (progress, nozzle, bed)
#define GAUGE_ROW2_Y    305         // row 2 center (part fan, aux fan, chamber fan)

// Header bar
#define HEADER_Y        14          // header bar top
#define HEADER_H        40          // header bar height

// LED progress bar
#define LED_BAR_H       8           // progress bar height

// Info/ETA line
#define INFO_LINE_Y     420         // ETA / pause / error text

// Bottom status bar
#define BOTTOM_BAR_Y    450         // filament / layer / speed

// =============================================================================
//  Font aliases (LovyanGFX FreeFont — smooth anti-aliased rendering)
//  These replace bitmap Font0-Font7 which look pixelated when scaled.
// =============================================================================
#define FONT_TITLE      &fonts::FreeSansBold24pt7b   // splash title, big labels
#define FONT_LARGE      &fonts::FreeSansBold18pt7b   // gauge main values, PAUSED/ERROR
#define FONT_MEDIUM     &fonts::FreeSans18pt7b       // header text, status
#define FONT_BODY       &fonts::FreeSans12pt7b       // body text, ETA, sub-values
#define FONT_SMALL      &fonts::FreeSans12pt7b       // labels, bottom bar, wifi
#define FONT_GAUGE_VAL  &fonts::FreeSansBold18pt7b   // gauge center value (temp/%)
#define FONT_GAUGE_SUB  &fonts::FreeSans12pt7b       // gauge sub-text (target temp)
#define FONT_GAUGE_LBL  &fonts::FreeSans12pt7b       // gauge label below arc

#endif // CONFIG_H
