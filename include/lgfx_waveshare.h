/*
 * LovyanGFX display driver for Waveshare ESP32-S3-Touch-LCD-4.3
 *
 * Hardware: 800x480 RGB parallel (ST7262), GT911 capacitive touch,
 *           CH422G IO expander for backlight/reset control.
 *
 * Pin assignments from Waveshare wiki:
 *   RGB data: R0-R4, G0-G5, B0-B4 (20 pins)
 *   Control:  DE=5, VSYNC=3, HSYNC=46, PCLK=7
 *   Touch:    SDA=8, SCL=9, IRQ=4 (I2C, GT911)
 *   IO Exp:   CH422G on I2C (handles LCD_BL, LCD_RST, TP_RST, SD_CS, USB_SEL)
 */

#ifndef LGFX_WAVESHARE_H
#define LGFX_WAVESHARE_H

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ESP32-S3 RGB panel/bus — not auto-included by LovyanGFX.hpp
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// ============================================================================
//  CH422G IO Expander addresses and bits
// ============================================================================
#define CH422G_WRITE_ADDR  0x46  // 7-bit: 0x23
#define CH422G_READ_ADDR   0x4D  // 7-bit: 0x26

// Output pin assignments on CH422G
#define CH422G_LCD_BL   (1 << 0)  // LCD backlight
#define CH422G_LCD_RST  (1 << 1)  // LCD reset (active low)
#define CH422G_TP_RST   (1 << 2)  // Touch reset (active low)
#define CH422G_SD_CS    (1 << 3)  // SD card CS
#define CH422G_USB_SEL  (1 << 4)  // USB select

// ============================================================================
//  LovyanGFX panel class for Waveshare 4.3" RGB
// ============================================================================
class LGFX_Waveshare43 : public lgfx::LGFX_Device {

  // RGB panel (ST7262 driver via parallel RGB interface)
  lgfx::Panel_RGB _panel_instance;
  lgfx::Bus_RGB   _bus_instance;
  lgfx::Touch_GT911 _touch_instance;

public:
  LGFX_Waveshare43(void) {
    // ---- Bus (RGB parallel) configuration ----
    {
      auto cfg = _bus_instance.config();

      cfg.panel = &_panel_instance;

      // Pixel clock
      cfg.freq_write = 16000000;  // 16 MHz pixel clock

      // Sync signals
      cfg.pin_d0  = GPIO_NUM_1;   // R0 (B0 in LovyanGFX RGB order - reversed)
      cfg.pin_d1  = GPIO_NUM_2;   // R1
      cfg.pin_d2  = GPIO_NUM_42;  // R2
      cfg.pin_d3  = GPIO_NUM_41;  // R3
      cfg.pin_d4  = GPIO_NUM_40;  // R4

      cfg.pin_d5  = GPIO_NUM_39;  // G0
      cfg.pin_d6  = GPIO_NUM_0;   // G1
      cfg.pin_d7  = GPIO_NUM_45;  // G2
      cfg.pin_d8  = GPIO_NUM_48;  // G3
      cfg.pin_d9  = GPIO_NUM_47;  // G4
      cfg.pin_d10 = GPIO_NUM_21;  // G5

      cfg.pin_d11 = GPIO_NUM_14;  // B0
      cfg.pin_d12 = GPIO_NUM_38;  // B1
      cfg.pin_d13 = GPIO_NUM_18;  // B2
      cfg.pin_d14 = GPIO_NUM_17;  // B3
      cfg.pin_d15 = GPIO_NUM_10;  // B4

      cfg.pin_henable = GPIO_NUM_5;   // DE (Data Enable)
      cfg.pin_vsync   = GPIO_NUM_3;   // VSYNC
      cfg.pin_hsync   = GPIO_NUM_46;  // HSYNC
      cfg.pin_pclk    = GPIO_NUM_7;   // PCLK

      // Timing parameters for 800x480
      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 8;
      cfg.hsync_pulse_width = 4;
      cfg.hsync_back_porch  = 8;

      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 8;
      cfg.vsync_pulse_width = 4;
      cfg.vsync_back_porch  = 8;

      cfg.pclk_active_neg   = 1;
      cfg.de_idle_high      = 0;
      cfg.pclk_idle_high    = 0;

      _bus_instance.config(cfg);
    }

    // ---- Panel configuration ----
    {
      auto cfg = _panel_instance.config();

      cfg.memory_width  = 800;
      cfg.memory_height = 480;
      cfg.panel_width   = 800;
      cfg.panel_height  = 480;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;

      _panel_instance.config(cfg);
    }

    _panel_instance.setBus(&_bus_instance);

    // ---- Touch (GT911 on I2C) configuration ----
    {
      auto cfg = _touch_instance.config();

      cfg.x_min = 0;
      cfg.x_max = 799;
      cfg.y_min = 0;
      cfg.y_max = 479;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;

      cfg.i2c_port = 0;  // I2C port 0
      cfg.i2c_addr = 0x14;  // GT911 alternate: 0x5D
      cfg.pin_sda  = GPIO_NUM_8;
      cfg.pin_scl  = GPIO_NUM_9;
      cfg.pin_int  = GPIO_NUM_4;
      cfg.freq     = 400000;

      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};

#endif // LGFX_WAVESHARE_H
