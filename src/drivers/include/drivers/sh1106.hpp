/*
 * Copyright 2026 mp3_esp32_fw contributors
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * SH1106 monochrome 128x64 OLED — I2C, LVGL display driver stub.
 *
 * Differences from the upstream `drivers::Display` (ST77XX, SPI, RGB565):
 *   - 1-bit monochrome pixel format (LV_COLOR_DEPTH=1).
 *   - I2C transport — share the project I2C bus rather than a dedicated SPI.
 *   - SH1106 has 132 columns of GDDRAM; only columns 2..129 are visible.
 *     Each "page" is 8 vertical pixels, so the panel is 8 pages tall.
 *   - Writes must be done page-at-a-time with column-address setup commands.
 *
 * Integration plan:
 *   - Build LVGL with LV_COLOR_DEPTH=1 in `sdkconfig.local` (or override in
 *     the lvgl component config).
 *   - In booting, replace the ST77XX Display::Create() call with
 *     drivers::Sh1106::Create().
 *   - The existing themes / screens in src/tangara/graphics + src/tangara/ui
 *     will mostly continue to work, but layouts designed for 240x135 colour
 *     need to be redesigned for 128x64 mono in a follow-up.
 */
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "lvgl/lvgl.h"

namespace drivers {

class Sh1106 {
 public:
  static constexpr uint16_t kWidthPx  = 128;
  static constexpr uint16_t kHeightPx = 64;
  static constexpr uint8_t  kPages    = kHeightPx / 8;  // 8

  /*
   * Initialises the panel over I2C, registers an LVGL display, and pushes the
   * default initialisation sequence. Returns nullptr on failure.
   */
  static auto Create() -> Sh1106*;

  ~Sh1106();

  auto SetDisplayOn(bool on) -> void;
  auto SetContrast(uint8_t level) -> void;  // 0..255 -> SH1106 register 0x81

  // LVGL flush callback (column-paged conversion + I2C blast).
  void OnLvglFlush(const lv_area_t* area, uint8_t* px_map);

  Sh1106(const Sh1106&) = delete;
  Sh1106& operator=(const Sh1106&) = delete;

 private:
  Sh1106();

  // TODO: I2C device handle, page framebuffer, LVGL display handle, etc.
  lv_display_t* display_ = nullptr;
};

}  // namespace drivers
