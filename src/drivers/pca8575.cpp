/*
 * Copyright 2026 mp3_esp32_fw contributors
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * PCA8575 driver implementation. See pca8575.hpp for the wiring map.
 *
 * Adapted from the upstream Tangara `drivers::Gpios` (gpios.cpp); the
 * transport and quasi-bidirectional handling are unchanged, only the pin
 * meanings and the I2C bus selection differ.
 */

#include "drivers/pca8575.hpp"

#include <cstdint>
#include <utility>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "hal/gpio_types.h"

#include "drivers/i2c.hpp"

namespace drivers {

namespace {

constexpr char kTag[] = "pca8575";

constexpr uint8_t  kAddress = 0x20;     // A0=A1=A2=GND
constexpr uint32_t kClockHz = 400'000;
constexpr gpio_num_t kIntPin = GPIO_NUM_34;

// Default port states. Bit set => pin driven/pulled HIGH (weak pull-up
// enabled for inputs, active-high output for outputs).
//
// Port 0 (P0_0..P0_7):
//   P00 FM_EN      output, default LOW (FM rail off at boot)        -> 0
//   P01            unused, leave as input-high                      -> 1
//   P02..P07       buttons, input-high                              -> 1 each
// => 0b1111'1110 = 0xFE
constexpr uint8_t kPort0Default = 0b11111110;

// Port 1 (P1_0..P1_7):
//   P10 BTN_LEFT       input-high                  -> 1
//   P11 SYS_PWR_EN_SW  input-high                  -> 1
//   P12 TPS65133_EN    output, default HIGH (rail enabled at boot,
//                                  matches external 10k pull-up)    -> 1
//   P13 unused, input-high                                          -> 1
//   P14 unused, input-high                                          -> 1
//   P15 HP_DETECT      input-high (active-low when plug inserted)   -> 1
//   P16 unused, input-high                                          -> 1
//   P17 unused, input-high                                          -> 1
// => 0xFF
constexpr uint8_t kPort1Default = 0b11111111;

constexpr uint16_t pack(uint8_t p0, uint8_t p1) {
  return (static_cast<uint16_t>(p1) << 8) | p0;
}

constexpr std::pair<uint8_t, uint8_t> unpack(uint16_t v) {
  return {static_cast<uint8_t>(v),
          static_cast<uint8_t>(v >> 8)};
}

}  // namespace

auto Pca8575::Create() -> Pca8575* {
  auto* self = new Pca8575();

  // INT line: input + internal pull-up (PCA8575 INT is open-drain).
  gpio_config_t int_cfg = {
      .pin_bit_mask = 1ULL << kIntPin,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  // GPIO34..39 are input-only and have no internal pull resistors; the
  // call still succeeds but the pull-up flag is silently ignored. The
  // PCB relies on an external pull-up to 3V3 in that case.
  gpio_config(&int_cfg);

  i2c_device_config_t cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = kAddress,
      .scl_speed_hz = kClockHz,
      .scl_wait_us = 0,
      .flags = {.disable_ack_check = false},
  };
  if (i2c_master_bus_add_device(i2c1_handle(), &cfg, &self->i2c_) != ESP_OK) {
    ESP_LOGE(kTag, "i2c_master_bus_add_device failed");
    delete self;
    return nullptr;
  }

  if (!self->Flush() || !self->Read()) {
    ESP_LOGE(kTag, "initial Flush/Read failed; chip not responding @ 0x%02x",
             kAddress);
    delete self;
    return nullptr;
  }

  ESP_LOGI(kTag, "PCA8575 ready @ 0x%02x (P0=0x%02x P1=0x%02x)",
           kAddress, kPort0Default, kPort1Default);
  return self;
}

Pca8575::Pca8575()
    : output_cache_(pack(kPort0Default, kPort1Default)),
      input_cache_(0) {}

Pca8575::~Pca8575() {
  if (i2c_) {
    i2c_master_bus_rm_device(i2c_);
    i2c_ = nullptr;
  }
}

auto Pca8575::WriteBuffered(Pin p, bool level) -> void {
  const uint16_t mask = static_cast<uint16_t>(1u << static_cast<int>(p));
  uint16_t cur = output_cache_.load();
  uint16_t next;
  do {
    next = level ? (cur | mask) : (cur & ~mask);
  } while (!output_cache_.compare_exchange_weak(cur, next));
}

auto Pca8575::WriteSync(Pin p, bool level) -> bool {
  WriteBuffered(p, level);
  return Flush();
}

auto Pca8575::Flush() -> bool {
  auto [p0, p1] = unpack(output_cache_.load());
  uint8_t data[2] = {p0, p1};
  esp_err_t err = i2c_master_transmit(i2c_, data, sizeof(data), 100);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "i2c_master_transmit failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

auto Pca8575::Read() -> bool {
  uint8_t data[2] = {0, 0};
  esp_err_t err = i2c_master_receive(i2c_, data, sizeof(data), 100);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "i2c_master_receive failed: %s", esp_err_to_name(err));
    return false;
  }
  input_cache_.store(pack(data[0], data[1]));
  return true;
}

auto Pca8575::Get(Pin p) const -> bool {
  const uint16_t bit = static_cast<uint16_t>(1u << static_cast<int>(p));
  return (input_cache_.load() & bit) != 0;
}

auto Pca8575::HasPendingInterrupt() const -> bool {
  return gpio_get_level(kIntPin) == 0;
}

}  // namespace drivers
