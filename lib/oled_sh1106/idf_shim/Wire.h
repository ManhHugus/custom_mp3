/*
 * Copyright 2026 mp3_esp32_fw contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Wire.h shim that adapts Arduino's TwoWire API onto ESP-IDF's
 * `driver/i2c_master.h` bus model. Backed by the shared bus owned by
 * drivers::init_i2c() (see drivers/i2c.cpp).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "Arduino.h"

#ifdef __cplusplus

class TwoWire {
 public:
  TwoWire();
  ~TwoWire();

  // Arduino API used by the OLED library.
  void begin();
  void begin(int sda, int scl);  // pins ignored — bus already configured.
  void setClock(uint32_t hz);

  void beginTransmission(uint8_t address);
  size_t write(uint8_t b);
  size_t write(const uint8_t* data, size_t n);
  uint8_t endTransmission();  // returns 0 on success.

 private:
  void EnsureDevice(uint8_t address);

  void* dev_ = nullptr;       // i2c_master_dev_handle_t (opaque here)
  uint8_t dev_address_ = 0;
  uint32_t clock_hz_ = 400000;

  static constexpr size_t kBufCapacity = 64;
  uint8_t tx_buf_[kBufCapacity];
  size_t tx_len_ = 0;
};

extern TwoWire Wire;
extern TwoWire Wire1;

#endif  // __cplusplus
