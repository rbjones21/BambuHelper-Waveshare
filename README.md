# BambuHelper

Dedicated Bambu Lab P1S printer monitor built with ESP32-S3 Super Mini and a 1.54" 240x240 color TFT display (ST7789).

Connects to your printer via MQTT over TLS and displays a real-time dashboard with arc gauges, animations, and live stats.

> **Note:** Currently tested and verified only with the Bambu Lab P1S. Other models may work but are not yet officially supported.

## Screenshots

| Dashboard | Web Interface - Settings | Web Interface - Gauge Colors |
|---|---|---|
| ![Dashboard](img/interface1.jpg) | ![Settings](img/screen1.png) | ![Gauge Colors](img/screen2.png) |

## Features

- **Live dashboard** - progress arc, temperature gauges, fan speed, layer count, time remaining
- **H2-style LED progress bar** - full-width glowing bar inspired by Bambu H2 series
- **Anti-aliased arc gauges** - smooth nozzle and bed temperature arcs with color zones
- **Animations** - loading spinner, progress pulse, completion celebration
- **Web config portal** - dark-themed settings page for WiFi, network, printer, display, and power settings
- **Network configuration** - DHCP or static IP, with optional IP display at startup
- **Display auto-off** - configurable timeout after print completion, auto-off when printer is off
- **NVS persistence** - all settings survive reboots
- **Auto AP mode** - creates WiFi hotspot on first boot or when WiFi is lost
- **Smart redraw** - only redraws changed UI elements for smooth performance
- **Customizable gauge colors** - per-gauge arc/label/value colors with preset themes

## Hardware

| Component | Specification |
|---|---|
| MCU | ESP32-S3 Super Mini |
| Display | 1.54" TFT SPI ST7789 (240x240) |
| Connection | SPI |

### Default Wiring

| Display Pin | ESP32-S3 GPIO |
|---|---|
| MOSI (SDA) | 11 |
| SCLK (SCL) | 12 |
| CS | 10 |
| DC | 9 |
| RST | 8 |
| BL | 13 |

Adjust pin assignments in `platformio.ini` build_flags to match your wiring.

## Flashing

1. Download the latest `BambuHelper-WebFlasher.bin` from [Releases](../../releases)
2. Open [ESP Web Flasher](https://espressif.github.io/esptool-js/) in Chrome or Edge
3. Connect your ESP32-S3 via USB
4. Click **Connect** and select your device
5. Set flash address to **0x0**
6. Select the downloaded `.bin` file
7. Click **Program**

## Setup

1. **Flash** the firmware (see above)
2. **Connect** to the `BambuHelper-XXXX` WiFi network (password: `bambu1234`)
3. **Open** `192.168.4.1` in your browser
4. **Enter** your home WiFi credentials and printer details:
   - Printer IP address (found in printer Settings > Network)
   - Serial number
   - LAN access code (8 characters, from printer Settings > Network)
5. **Save** - the device restarts and connects to your printer

## Web Interface

The built-in web interface (accessible at the device's IP address) provides the following settings:

### WiFi Settings
- **SSID** - your home WiFi network name
- **Password** - WiFi password

### Network
- **IP Assignment** - choose between DHCP (automatic) or Static IP
- **Static IP fields** (when static is selected):
  - IP Address
  - Gateway
  - Subnet Mask
  - DNS Server
- **Show IP at startup** - display the assigned IP on screen for 3 seconds after WiFi connects (on by default)

### Printer Settings
- **Enable Monitoring** - toggle printer connection on/off
- **Printer Name** - friendly name shown on dashboard header
- **Printer IP Address** - LAN IP of your Bambu printer
- **Serial Number** - printer serial number
- **LAN Access Code** - 8-character code from printer network settings
- **Live Stats** - real-time nozzle/bed temp, progress, fan speed, and connection status

### Display
- **Brightness** - backlight level (10–255)
- **Screen Rotation** - 0°, 90°, 180°, 270°
- **Display off after print complete** - minutes to show the finish screen before turning off the display (0 = never turn off, default: 3 minutes)
- **Keep display always on** - override the timeout and never turn off

### Gauge Colors
- **Theme presets** - Default, Mono Green, Neon, Warm, Ocean
- **Background color** - display background
- **Track color** - inactive arc background
- **Per-gauge colors** (arc, label, value) for:
  - Progress
  - Nozzle temperature
  - Bed temperature
  - Part fan
  - Aux fan
  - Chamber fan

### Other
- **Factory Reset** - erases all settings and restarts

## Dashboard Screens

| Screen | When |
|---|---|
| Splash | Boot (2 seconds) |
| AP Mode | First boot / no WiFi configured |
| Connecting WiFi | Attempting WiFi connection |
| WiFi Connected | Shows IP for 3s (if enabled) |
| Connecting Printer | WiFi connected, waiting for MQTT |
| Idle | Connected, printer not printing |
| Printing | Active print with full dashboard |
| Finished | Print complete with animation (auto-off after timeout) |
| Display Off | After finish timeout or printer powered off |

## Display Power Management

- After a print completes, the finish screen is shown for a configurable duration (default: 3 minutes), then the display and backlight turn off to save power.
- When the printer is powered off or disconnected, the display stays off.
- When the printer comes back online or starts a new print, the display automatically wakes up.
- The "Keep display always on" option overrides the auto-off behavior.
- The web interface shows the current display state (on/off) in the live status section.

## Project Structure

```
include/
  config.h              Pin definitions, colors, constants
  bambu_state.h         Data structures (BambuState, PrinterConfig)
src/
  main.cpp              Setup/loop orchestrator
  settings.cpp          NVS persistence (WiFi, network, printer, display, power)
  wifi_manager.cpp      WiFi STA + AP fallback, static IP support
  web_server.cpp        Config portal (HTML embedded)
  bambu_mqtt.cpp        MQTT over TLS, delta merge
  display_ui.cpp        Screen state machine
  display_gauges.cpp    Arc gauges, progress bar, temp gauges
  display_anim.cpp      Animations (spinner, pulse, dots)
  icons.h               16x16 pixel-art icons
```

## Requirements

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- Bambu Lab printer with LAN mode enabled
- Printer and ESP32 on the same local network

## Future Plans

- Multi-printer monitoring (up to 4 printers)
- Physical buttons for switching between printers
- OTA firmware updates
- Additional printer model support (X1C, A1, P1P)

## License

MIT
