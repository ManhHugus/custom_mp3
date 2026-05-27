/*
 * Copyright 2026 mp3_esp32_fw contributors
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * PCA8575 16-bit quasi-bidirectional I2C GPIO expander.
 *
 * Wiring (per board schematic, May 2026):
 *
 *   SDA  -> ESP32 GPIO21    (drivers::i2c1_handle() bus)
 *   SCL  -> ESP32 GPIO22
 *   INT  -> ESP32 GPIO34    (input only, with internal pull-up)
 *   ADDR : A0=A1=A2=GND  ->  7-bit I2C address 0x20
 *
 *   P00  FM_EN              output, drives EN/UVLO of TPS25961 (FM rail).
 *                           Default LOW (FM tuner off at boot).
 *   P01  -                  unused (NC).
 *   P02  BTN_OK             input, active-low button.
 *   P03  BTN_BACK           input, active-low button.
 *   P04  BTN_RIGHT          input, active-low button.
 *   P05  BTN_VOL_UP         input, active-low button.
 *   P06  BTN_CENTER         input, active-low button.
 *   P07  BTN_VOL_DOWN       input, active-low button.
 *
 *   P10  BTN_LEFT           input, active-low button.
 *   P11  SYS_PWR_EN_SW      input, slide switch on EN of TLV75533PDBV.
 *                           We only read it (the switch drives the rail
 *                           directly); reflected here so the UI can show
 *                           power state.
 *   P12  TPS65133_EN        output, drives EN of TPS65133DPDR (negative-rail
 *                           buck-boost). External 10k pull-up to 3V3 keeps
 *                           the rail enabled at boot; we assert HIGH so the
 *                           quasi-bidirectional pin doesn't fight the
 *                           pull-up. Drive LOW to disable the rail.
 *   P13  -                  unused (NC).
 *   P14  -                  unused (NC).
 *   P15  HP_DETECT          input, 3.5 mm jack switch contact (SJ-3506-SMT).
 *                           Active-low when a plug is inserted.
 *   P16  -                  unused (NC).
 *   P17  -                  unused (NC).
 *
 * Protocol recap:
 *   - Read 2 bytes  -> current state of P0/P1 ports (inputs).
 *   - Write 2 bytes -> drive outputs (any pin used as input must first be
 *                      written HIGH so its weak pull-up is enabled).
 *   - INT pin (open-drain) asserts low on any input level change; we tie
 *     it to GPIO34 with an internal pull-up and poll on falling edges so
 *     we don't free-run I2C reads.
 */
#pragma once

#include <atomic>
#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "hal/gpio_types.h"

namespace drivers {

class Pca8575 {
 public:
  /*
   * Logical pin labels. Bit position == hardware pin position:
   *   bits 0..7  = P0_0..P0_7
   *   bits 8..15 = P1_0..P1_7
   */
  enum class Pin : uint8_t {
    // Port 0
    kFmEn        = 0,   // output  -> TPS25961 EN/UVLO (FM tuner rail)
    kP01Unused   = 1,
    kBtnOk       = 2,   // input, active-low
    kBtnBack     = 3,   // input, active-low
    kBtnRight    = 4,   // input, active-low
    kBtnVolUp    = 5,   // input, active-low
    kBtnCenter   = 6,   // input, active-low
    kBtnVolDown  = 7,   // input, active-low

    // Port 1
    kBtnLeft     = 8,   // input, active-low
    kSysPwrEnSw  = 9,   // input, slide switch on TLV75533 EN
    kTps65133En  = 10,  // output -> TPS65133 EN (negative rail)
    kP13Unused   = 11,
    kP14Unused   = 12,
    kHpDetect    = 13,  // input, 3.5 mm jack detect (SJ-3506)
    kP16Unused   = 14,
    kP17Unused   = 15,
  };

  static auto Create() -> Pca8575*;
  ~Pca8575();
  Pca8575(const Pca8575&) = delete;
  Pca8575& operator=(const Pca8575&) = delete;

  /* Read both ports from the chip into the input cache. */
  auto Read() -> bool;

  /* Cached pin level from the last successful Read() / Flush(). */
  auto Get(Pin p) const -> bool;

  /* Set an output pin and flush both port bytes back to the chip. */
  auto WriteSync(Pin p, bool level) -> bool;

  /* Buffer an output bit for the next Flush(); does not touch the bus. */
  auto WriteBuffered(Pin p, bool level) -> void;

  /* Push the buffered output state to the chip. */
  auto Flush() -> bool;

  /*
   * True if the INT line is currently asserted (active-low, GPIO34).
   * Use as a "should I poll the chip now?" hint from a task loop.
   */
  auto HasPendingInterrupt() const -> bool;

  /* Convenience semantic helpers. */
  auto SetFmRail(bool on) -> bool       { return WriteSync(Pin::kFmEn, on); }
  auto SetNegativeRail(bool on) -> bool { return WriteSync(Pin::kTps65133En, on); }
  auto IsHeadphoneInserted() const -> bool { return !Get(Pin::kHpDetect); }
  auto IsSysPowerSwitchOn() const -> bool  { return Get(Pin::kSysPwrEnSw); }

 private:
  Pca8575();

  i2c_master_dev_handle_t i2c_ = nullptr;
  std::atomic<uint16_t> output_cache_{0};
  std::atomic<uint16_t> input_cache_{0};
};

}  // namespace drivers
