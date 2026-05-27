/*
 * Copyright 2026 mp3_esp32_fw contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "Wire.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

// Implemented by the host project (drivers/i2c.cpp wrapper) so the OLED
// component does not have to depend on the drivers component.
extern "C" i2c_master_bus_handle_t oled_sh1106_get_i2c_bus(void);

namespace {
constexpr char kTag[] = "oled_wire";
}

TwoWire::TwoWire() = default;

TwoWire::~TwoWire() {
  if (dev_) {
    i2c_master_bus_rm_device(static_cast<i2c_master_dev_handle_t>(dev_));
    dev_ = nullptr;
  }
}

void TwoWire::begin() {}
void TwoWire::begin(int /*sda*/, int /*scl*/) {}

void TwoWire::setClock(uint32_t hz) {
  if (hz == clock_hz_) return;
  clock_hz_ = hz;
  if (dev_) {
    // Recreate the device with the new clock speed on next use.
    i2c_master_bus_rm_device(static_cast<i2c_master_dev_handle_t>(dev_));
    dev_ = nullptr;
  }
}

void TwoWire::EnsureDevice(uint8_t address) {
  if (dev_ && dev_address_ == address) return;
  if (dev_) {
    i2c_master_bus_rm_device(static_cast<i2c_master_dev_handle_t>(dev_));
    dev_ = nullptr;
  }
  i2c_device_config_t cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = address,
      .scl_speed_hz = clock_hz_,
      .scl_wait_us = 0,
      .flags = {.disable_ack_check = false},
  };
  i2c_master_dev_handle_t dev;
  esp_err_t err = i2c_master_bus_add_device(oled_sh1106_get_i2c_bus(), &cfg, &dev);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "add_device 0x%02x failed: %s", address, esp_err_to_name(err));
    dev_ = nullptr;
    return;
  }
  dev_ = dev;
  dev_address_ = address;
}

void TwoWire::beginTransmission(uint8_t address) {
  EnsureDevice(address);
  tx_len_ = 0;
}

size_t TwoWire::write(uint8_t b) {
  if (tx_len_ >= kBufCapacity) return 0;
  tx_buf_[tx_len_++] = b;
  return 1;
}

size_t TwoWire::write(const uint8_t* data, size_t n) {
  size_t can = kBufCapacity - tx_len_;
  if (n > can) n = can;
  for (size_t i = 0; i < n; ++i) tx_buf_[tx_len_++] = data[i];
  return n;
}

uint8_t TwoWire::endTransmission() {
  if (!dev_ || tx_len_ == 0) {
    tx_len_ = 0;
    return dev_ ? 0 : 4;
  }
  esp_err_t err = i2c_master_transmit(
      static_cast<i2c_master_dev_handle_t>(dev_), tx_buf_, tx_len_, 100);
  tx_len_ = 0;
  return err == ESP_OK ? 0 : 4;
}

TwoWire Wire;
TwoWire Wire1;
