/*
 * Copyright 2026 mp3_esp32_fw contributors
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Central GPIO / bus pin map for this board.
 *
 * This is a fork of Tangara hardware: the MCU is still ESP32-WROVER-E, but
 * many peripherals differ (SGTL5000 audio codec, SH1106 OLED, PCA8575 GPIO
 * expander, FM tuner, eMMC, ATSAMD21E18A-AF as power MCU).
 *
 * Fill the placeholders below in once the schematic is finalised. Anything
 * still tagged `TODO(pinmap)` must be set before flashing real hardware.
 */
#pragma once

#include <cstdint>
#include "driver/gpio.h"

namespace board {

// ---------------------------------------------------------------------------
// I2C0 — SH1106 OLED (and any other SDA=4/SCL=2 peripherals).
// ---------------------------------------------------------------------------
constexpr gpio_num_t kI2C0Sda  = GPIO_NUM_4;
constexpr gpio_num_t kI2C0Scl  = GPIO_NUM_2;
constexpr uint32_t   kI2C0Hz   = 400'000;

// I2C1 — PCA8575 GPIO expander (buttons, rail enables, jack detect).
constexpr gpio_num_t kI2C1Sda  = GPIO_NUM_21;
constexpr gpio_num_t kI2C1Scl  = GPIO_NUM_22;
constexpr uint32_t   kI2C1Hz   = 400'000;

// 7-bit I2C addresses (datasheet values; verify ADDR pin straps on your PCB).
constexpr uint8_t kAddrSgtl5000 = 0x0A;  // or 0x2A depending on CTRL_ADR0
constexpr uint8_t kAddrSh1106   = 0x3C;  // or 0x3D depending on SA0
constexpr uint8_t kAddrPca8575  = 0x20;  // base; A0..A2 set the low 3 bits
constexpr uint8_t kAddrFmTuner  = 0x00;  // TODO(pinmap): set when FM IC chosen

// ---------------------------------------------------------------------------
// I2S0 — SGTL5000 audio data. SGTL5000 *requires* an MCLK from the ESP32
//        (or external XO). Generate from APLL: mclk = 256 * fs typically.
// ---------------------------------------------------------------------------
constexpr gpio_num_t kI2SMclk  = GPIO_NUM_NC;  // TODO(pinmap)
constexpr gpio_num_t kI2SBclk  = GPIO_NUM_NC;  // TODO(pinmap)
constexpr gpio_num_t kI2SLrclk = GPIO_NUM_NC;  // TODO(pinmap)
constexpr gpio_num_t kI2SDout  = GPIO_NUM_NC;  // TODO(pinmap)  ESP32 -> codec
constexpr gpio_num_t kI2SDin   = GPIO_NUM_NC;  // optional, codec -> ESP32

// ---------------------------------------------------------------------------
// SPI (HSPI/VSPI) — link to ATSAMD21E18A-AF power MCU.
// ESP32 is master.
// ---------------------------------------------------------------------------
constexpr gpio_num_t kSamdSpiSck  = GPIO_NUM_NC;  // TODO(pinmap)
constexpr gpio_num_t kSamdSpiMosi = GPIO_NUM_NC;  // TODO(pinmap)
constexpr gpio_num_t kSamdSpiMiso = GPIO_NUM_NC;  // TODO(pinmap)
constexpr gpio_num_t kSamdSpiCs   = GPIO_NUM_NC;  // TODO(pinmap)
constexpr gpio_num_t kSamdIrq     = GPIO_NUM_NC;  // SAMD -> ESP32 attention line

// ---------------------------------------------------------------------------
// eMMC — preferred bus is SDMMC 4-bit; falls back to SPI if your routing
//        forced SPI. Pick one and remove the other set.
//        On ESP32 classic, SDMMC slot 1 uses fixed pins:
//          CMD=15  CLK=14  D0=2  D1=4  D2=12  D3=13
// ---------------------------------------------------------------------------
constexpr bool       kEmmcUseSdmmc4Bit = true;   // TODO(pinmap): confirm
constexpr gpio_num_t kEmmcSpiCs        = GPIO_NUM_NC;  // TODO(pinmap) if SPI

// ---------------------------------------------------------------------------
// PCA8575 — INT line into ESP32 for interrupt-driven button reads.
// ---------------------------------------------------------------------------
constexpr gpio_num_t kPca8575Int = GPIO_NUM_34;

// ---------------------------------------------------------------------------
// SH1106 OLED — optional RESET line (most modules tie it to RC reset and
//               omit; set to GPIO_NUM_NC if unused).
// ---------------------------------------------------------------------------
constexpr gpio_num_t kOledReset = GPIO_NUM_NC;  // TODO(pinmap) (optional)

// ---------------------------------------------------------------------------
// Misc — battery sense ADC, amplifier mute, headphone-detect, etc.
// ---------------------------------------------------------------------------
constexpr gpio_num_t kAmpMute      = GPIO_NUM_NC;  // TODO(pinmap)
constexpr gpio_num_t kHeadphoneDet = GPIO_NUM_NC;  // TODO(pinmap)

}  // namespace board
