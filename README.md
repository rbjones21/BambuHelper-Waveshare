# BambuHelper — Waveshare ESP32-S3-Touch-LCD-4.3

> **Fork of [Keralots/BambuHelper](https://github.com/Keralots/BambuHelper)** ported to the [Waveshare ESP32-S3-Touch-LCD-4.3](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3) (800x480 RGB, capacitive touch).

Dedicated Bambu Lab printer monitor with a 4.3" color touchscreen. Connects to your printer via MQTT over TLS and displays a real-time dashboard with arc gauges, animations, and live stats.

### What changed from the original

| Area | Original | This fork |
|---|---|---|
| **Display** | 1.54" ST7789 SPI (240x240) | 4.3" ST7262 RGB parallel (800x480) |
| **Graphics library** | TFT_eSPI | LovyanGFX |
| **Input** | Physical button / TTP223 touch | GT911 capacitive touchscreen (tap anywhere) |
| **Backlight** | PWM via GPIO | CH422G IO expander (on/off) |
| **MCU** | ESP32-S3 Super Mini | ESP32-S3 (on-board, 16MB flash, 8MB PSRAM) |
| **Fonts** | Bitmap (scaled) | FreeFont (anti-aliased) |

All printer communication (MQTT, cloud auth, web config) is unchanged from upstream.

### Supported Printers

| Connection Mode | Printers | How it connects |
|---|---|---|
| **LAN Direct** | P1P, P1S, X1, X1C, X1E, A1, A1 Mini | Local MQTT via printer IP + LAN access code |
| **Bambu Cloud (All printers)** | Any Bambu printer | Cloud MQTT via access token — no LAN mode needed |

> **Tip:** Use "Bambu Cloud (All printers)" if you don't want to enable LAN mode on your printer (e.g. to keep Bambu Handy working), if your ESP32 is on a different network than the printer, or if your printer only supports cloud mode (H2C, H2D, H2S, P2S).

### Cloud Mode Security Notice

When using Bambu Cloud, BambuHelper connects through Bambu Lab's cloud MQTT service:

- **No credentials are stored** — you extract an access token from your browser and paste it into the web interface.
- **Only the access token is stored** in flash. It expires after ~3 months.
- **Read-only access** — BambuHelper only reads printer status, never sends commands.
- **Same approach** as [Home Assistant Bambu Lab integration](https://github.com/greghesp/ha-bambulab), [OctoPrint-Bambu](https://github.com/jneilliii/OctoPrint-BambuPrinter), and other community tools.

## Hardware

| Component | Specification |
|---|---|
| Device | [Waveshare ESP32-S3-Touch-LCD-4.3](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3) |
| Display | 4.3" 800x480 RGB parallel (ST7262) |
| Touch | GT911 capacitive (I2C) |
| MCU | ESP32-S3 (on-board), 16MB flash, 8MB PSRAM |
| IO Expander | CH422G (backlight, LCD/touch reset) |

**No wiring required** — the display, touch controller, and ESP32-S3 are all integrated on the Waveshare board. Just plug in USB-C for power.

Optional: Passive buzzer on GPIO 5 for print finish/error notifications.

## Flashing

### Using PlatformIO (recommended for development)

1. Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
2. Clone this repository
3. Open the project in VS Code
4. Connect the Waveshare board via USB-C
5. Click **Upload** (or `pio run -t upload`)

> **Note:** The Waveshare board uses USB CDC — it should appear as a serial port automatically. If not, hold the BOOT button while plugging in USB, then release after connecting.

### Using ESP Web Flasher

1. Download the latest firmware `.bin` from [Releases](../../releases)
2. Open [ESP Web Flasher](https://espressif.github.io/esptool-js/) in Chrome or Edge
3. Connect the Waveshare board via USB-C
4. Click **Connect** and select your device
5. Set flash address to **0x0**
6. Select the downloaded `.bin` file
7. Click **Program**

## Setup

1. **Flash** the firmware (see above)
2. **Connect** to the `BambuHelper-XXXX` WiFi network (password: `bambu1234`)
3. **Open** `192.168.4.1` in your browser
4. **Enter** your home WiFi credentials and **Save** — the device restarts and connects
5. **Note the IP address** shown on the display after WiFi connects
6. **Open** that IP in your browser to access the full web interface
7. **Configure your printer:**

   **LAN Direct** (P1P, P1S, X1, X1C, X1E, A1, A1 Mini):
   - Printer IP address (found in printer Settings > Network)
   - Serial number (15-character code, e.g. `01P00A000000000`)
   - LAN access code (8 characters, from printer Settings > Network)

   **Bambu Cloud (All printers):**
   - Get your access token from your browser (see [Getting a Cloud Token](#getting-a-cloud-token))
   - Paste the token into the web interface
   - Enter your printer's serial number

8. **Save Printer Settings** — the device connects to your printer

### Getting a Cloud Token

**Using browser DevTools (Chrome / Edge):**
1. Open https://bambulab.com and log in
2. Press **F12** → **Application** tab → **Cookies** → `https://bambulab.com`
3. Find the `token` row, double-click the Value, copy it
4. Paste into BambuHelper's "Access Token" field

**Using the Python helper script:**
```bash
pip install curl_cffi
python tools/get_token.py
```

> The token expires after ~3 months. When it does, repeat the process above.

## Features

- **Live dashboard** — progress arc, temperature gauges, fan speed, layer count, ETA
- **Anti-aliased rendering** — smooth FreeFont text and filled arc gauges at 800x480
- **H2-style LED progress bar** — full-width glowing bar
- **Animations** — loading spinner, progress shimmer, completion celebration
- **Touch input** — tap anywhere to cycle printers or wake display
- **Web config portal** — dark-themed settings page for WiFi, printer, display, and power
- **Multi-printer** — monitor up to 2 printers with smart auto-rotation
- **Display power management** — auto-off after print, clock mode, always-on option
- **NVS persistence** — all settings survive reboots

## Web Interface

The built-in web interface (accessible at the device's IP) provides:

- **WiFi** — SSID, password
- **Network** — DHCP or static IP, optional IP display at startup
- **Printer** — LAN Direct or Cloud mode, live connection stats
- **Display** — brightness (on/off on this board), rotation, auto-off timer, clock mode
- **Gauge Colors** — theme presets (Default, Mono Green, Neon, Warm, Ocean), per-gauge arc/label/value colors
- **Multi-Printer** — rotation mode (Smart / Auto-rotate / Manual), rotation interval
- **Factory Reset**

## Project Structure

```
include/
  config.h              Display constants, colors, layout, font aliases
  bambu_state.h         Data structures (BambuState, PrinterConfig, ConnMode)
  lgfx_waveshare.h      LovyanGFX driver for Waveshare 4.3" (RGB pins, GT911 touch, CH422G)
src/
  main.cpp              Setup/loop orchestrator
  settings.cpp          NVS persistence
  wifi_manager.cpp      WiFi STA + AP fallback
  web_server.cpp        Config portal (HTML embedded)
  bambu_mqtt.cpp        MQTT over TLS, delta merge
  bambu_cloud.cpp       Bambu Cloud helpers (region URLs, JWT)
  button.cpp            Touch input (GT911 tap detection)
  display_ui.cpp        Screen state machine, CH422G backlight control
  display_gauges.cpp    Filled arc gauges, progress bar, temp gauges
  display_anim.cpp      Animations (spinner, pulse, dots)
  clock_mode.cpp        Digital clock display
  clock_pong.cpp        Pong/Breakout animated clock
  icons.h               16x16 / 32x32 pixel-art icons
tools/
  get_token.py          Python helper to get Bambu Cloud token
```

## Merging Upstream Changes

This fork tracks [Keralots/BambuHelper](https://github.com/Keralots/BambuHelper). To pull in upstream updates:

```bash
git remote add upstream https://github.com/Keralots/BambuHelper.git
git fetch upstream
git merge upstream/main
```

Conflicts will typically be in the display files (`display_ui.cpp`, `display_gauges.cpp`, `config.h`, `platformio.ini`) since those are the files this fork modifies. The MQTT, web server, settings, and cloud code should merge cleanly.

## Acknowledgments

- **[Keralots/BambuHelper](https://github.com/Keralots/BambuHelper)** — original project, all printer communication and web interface code
- **[LovyanGFX](https://github.com/lovyan03/LovyanGFX)** — display graphics library
- **[Waveshare](https://www.waveshare.com/)** — ESP32-S3-Touch-LCD-4.3 hardware

## License

MIT
