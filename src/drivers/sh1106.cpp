/*
 * Copyright 2026 mp3_esp32_fw contributors
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * SH1106 OLED driver — bridges LVGL's 1-bpp render buffer into the
 * ThingPulse OLED library (lib/oled_sh1106) which talks to the panel
 * over our project I2C bus.
 */
#include "drivers/sh1106.hpp"

#include <cstring>
#include <memory>

#include "esp_log.h"

#include "SH1106Wire.h"

#include "drivers/i2c.hpp"

// Hook consumed by lib/oled_sh1106/idf_shim/Wire.cpp so the OLED component
// does not have to depend on the drivers component.
extern "C" i2c_master_bus_handle_t oled_sh1106_get_i2c_bus(void) {
  return drivers::i2c_handle();
}

namespace drivers {

namespace {
constexpr char kTag[] = "sh1106";
constexpr uint8_t kI2cAddress = 0x3C;  // Standard SH1106 7-bit address.

SH1106Wire* sPanel = nullptr;

void FlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  auto* self = static_cast<Sh1106*>(lv_display_get_user_data(disp));
  self->OnLvglFlush(area, px_map);
  lv_display_flush_ready(disp);
}
}  // namespace

Sh1106::Sh1106() = default;

Sh1106::~Sh1106() {
  if (display_) {
    lv_display_delete(display_);
    display_ = nullptr;
  }
  if (sPanel) {
    sPanel->end();
    delete sPanel;
    sPanel = nullptr;
  }
}

auto Sh1106::Create() -> Sh1106* {
  ESP_LOGI(kTag, "Init SH1106 128x64 OLED @ 0x%02x", kI2cAddress);

  auto* panel = new SH1106Wire(kI2cAddress, /*sda=*/-1, /*scl=*/-1,
                               GEOMETRY_128_64);
  if (!panel->init()) {
    ESP_LOGE(kTag, "SH1106 init failed");
    delete panel;
    return nullptr;
  }
  panel->clear();
  panel->display();
  sPanel = panel;

  std::unique_ptr<Sh1106> self(new Sh1106());

  // LVGL I1 partial render mode: 1 bit per pixel, MSB-first. Two
  // 16-row tiles is enough for LVGL to double-buffer.
  static constexpr size_t kBufBytes = kWidthPx * 16 / 8;  // 256 bytes
  static uint8_t buf1[kBufBytes];
  static uint8_t buf2[kBufBytes];

  self->display_ = lv_display_create(kWidthPx, kHeightPx);
  lv_display_set_color_format(self->display_, LV_COLOR_FORMAT_I1);
  lv_display_set_buffers(self->display_, buf1, buf2, sizeof(buf1),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_user_data(self->display_, self.get());
  lv_display_set_flush_cb(self->display_, FlushCb);
  lv_display_set_default(self->display_);

  return self.release();
}

auto Sh1106::SetDisplayOn(bool on) -> void {
  if (!sPanel) return;
  if (on) sPanel->displayOn();
  else sPanel->displayOff();
}

auto Sh1106::SetContrast(uint8_t level) -> void {
  if (!sPanel) return;
  sPanel->setContrast(level);
}

void Sh1106::OnLvglFlush(const lv_area_t* area, uint8_t* px_map) {
  if (!sPanel) return;

  // LVGL I1 layout: 1 bpp, MSB-first within a byte, packed row-major.
  // SH1106Wire framebuffer is column-major: byte[x + page*128], page = y/8,
  // bit position = y%8 (LSB is the top pixel of the page).
  const int32_t x1 = area->x1, x2 = area->x2;
  const int32_t y1 = area->y1, y2 = area->y2;
  const uint16_t w_bits = static_cast<uint16_t>(x2 - x1 + 1);
  const uint16_t stride = static_cast<uint16_t>((w_bits + 7) / 8);

  uint8_t* fb = sPanel->buffer;
  for (int32_t y = y1; y <= y2; ++y) {
    const uint8_t* row = px_map + (y - y1) * stride;
    const uint8_t page = static_cast<uint8_t>(y >> 3);
    const uint8_t mask = static_cast<uint8_t>(1u << (y & 0x07));
    for (int32_t x = x1; x <= x2; ++x) {
      const uint16_t bit = static_cast<uint16_t>(x - x1);
      const uint8_t src = row[bit >> 3];
      const bool on = (src >> (7 - (bit & 7))) & 1;
      uint8_t& dst = fb[x + page * kWidthPx];
      if (on) dst |= mask;
      else    dst &= static_cast<uint8_t>(~mask);
    }
  }

  if (lv_display_flush_is_last(display_)) {
    sPanel->display();
  }
}

}  // namespace drivers
