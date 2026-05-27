/*
 * Copyright 2026 mp3_esp32_fw contributors
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Driver for the RDA Microelectronics RDA5807FP single-chip FM broadcast
 * tuner.  Communicates over I2C using the chip's two 7-bit addresses:
 *
 *   0x10  - "sequential" mode (read/write a contiguous register block
 *           starting at 0x02 for writes / 0x0A for reads).
 *   0x11  - "random" mode      (write a register index byte, then 16-bit
 *           data, big endian).
 *
 * Datasheet: "RDA5807FP - SINGLE-CHIP BROADCAST FM RADIO TUNER".
 *
 * Implementation notes:
 *   * Register map matches the RDA5807M; this driver uses big-endian 16-bit
 *     registers throughout.
 *   * RDS PS (8-character station name) is decoded internally; the higher-
 *     level radio text (RT) buffers are exposed as raw groups so that the
 *     UI / Lua side can decide how aggressively to decode them.
 *   * The FM rail (TPS25961) is gated by PCA8575::kFmEn; callers must
 *     enable that rail before Init().
 */
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "driver/i2c_master.h"

namespace drivers {

class Rda5807 {
 public:
  // Hardware I2C addresses.
  static constexpr uint8_t kAddrSequential = 0x10;  // block reads
  static constexpr uint8_t kAddrRandom     = 0x11;  // index addressed

  enum class Band : uint8_t {
    kUsEurope    = 0,  // 87.0 - 108.0 MHz
    kJapanWide   = 1,  // 76.0 - 91.0 MHz
    kWorld       = 2,  // 76.0 - 108.0 MHz
    kEastEurope  = 3,  // 65.0 - 76.0 MHz
  };

  enum class Space : uint8_t {
    k100kHz = 0,
    k200kHz = 1,
    k50kHz  = 2,
    k25kHz  = 3,
  };

  enum class SeekDir : uint8_t { kDown = 0, kUp = 1 };

  struct RdsSnapshot {
    bool synced = false;
    bool has_ps = false;     // station-name buffer is non-empty.
    bool has_pty = false;
    char ps[9]   = {0};      // 8-char + NUL.
    char rt[65]  = {0};      // 64-char + NUL (radio text).
    uint8_t  pty = 0;
    uint16_t pi  = 0;        // Programme Identification.
  };

  /*
   * Allocate, probe and power up the chip on the given I2C bus.  Returns
   * nullptr on any failure (chip absent, FM rail not enabled, etc.).
   */
  static auto Create(i2c_master_bus_handle_t bus) -> Rda5807*;

  ~Rda5807();
  Rda5807(const Rda5807&)            = delete;
  Rda5807& operator=(const Rda5807&) = delete;

  // ---- Power / chip state ----------------------------------------------
  auto PowerUp()   -> bool;
  auto PowerDown() -> bool;
  auto SoftReset() -> bool;
  auto IsReady()   -> bool;          // FM_READY status flag.

  // ---- Tuning ----------------------------------------------------------
  auto SetBand(Band b)               -> bool;
  auto GetBand() const -> Band       { return band_; }
  auto SetSpace(Space s)             -> bool;
  auto GetSpace() const -> Space     { return space_; }

  auto SetFrequency(uint32_t khz)    -> bool;   // e.g. 103900
  auto GetFrequency() const -> uint32_t { return freq_khz_; }
  auto FrequencyUp()                 -> bool;
  auto FrequencyDown()               -> bool;
  auto BandLowKHz()  const -> uint32_t;
  auto BandHighKHz() const -> uint32_t;
  auto SpaceKHz()    const -> uint32_t;

  // ---- Seek ------------------------------------------------------------
  // Non-blocking start; poll IsSeekComplete() and call FinishSeek() when
  // it returns true.  wrap=true continues past band edges.
  auto StartSeek(SeekDir dir, bool wrap = true) -> bool;
  auto IsSeekComplete() -> bool;     // true once STC is set.
  auto FinishSeek() -> bool;         // returns false if SF (seek-fail).
  auto SetSeekThreshold(uint8_t snr) -> bool;  // 0..15

  // ---- Audio -----------------------------------------------------------
  auto SetMute(bool muted)  -> bool;
  auto IsMuted() const -> bool { return muted_; }
  auto SetMono(bool mono)   -> bool;
  auto SetBass(bool on)     -> bool;
  auto SetVolume(uint8_t v) -> bool;            // 0..15
  auto GetVolume() const -> uint8_t { return volume_; }

  // ---- Status / signal -------------------------------------------------
  auto GetRssi()    -> uint8_t;      // 0..63 (logarithmic).
  auto IsStereo()   -> bool;
  auto IsTuned()    -> bool;         // FM_TRUE flag.

  // ---- RDS -------------------------------------------------------------
  auto SetRds(bool enable) -> bool;
  auto IsRdsEnabled() const -> bool { return rds_enabled_; }
  // Polls the chip; if a new group is ready, consumes it and updates the
  // internal PS / RT buffers.  Returns true iff something changed.
  auto PollRds() -> bool;
  auto GetRds() const -> RdsSnapshot { return rds_; }
  auto ClearRds() -> void;

 private:
  Rda5807(i2c_master_bus_handle_t bus,
          i2c_master_dev_handle_t random,
          i2c_master_dev_handle_t sequential);

  // Low-level register I/O.
  auto WriteReg(uint8_t reg, uint16_t val) -> bool;
  auto ReadReg(uint8_t reg, uint16_t& out) -> bool;
  auto ReadStatusBlock() -> bool;  // 0x0A..0x0F into status_[].

  auto ApplyShadow(uint8_t reg) -> bool;
  auto WaitForTuneComplete(uint32_t timeout_ms = 200) -> bool;
  auto DecodeRdsGroup() -> bool;

  i2c_master_bus_handle_t bus_;
  i2c_master_dev_handle_t random_;       // 0x11
  i2c_master_dev_handle_t sequential_;   // 0x10

  // Shadow of the writeable register set (0x02..0x07), big-endian values.
  std::array<uint16_t, 8> shadow_{};
  // Most recently read status block (0x0A..0x0F).
  std::array<uint16_t, 6> status_{};

  Band     band_     = Band::kUsEurope;
  Space    space_    = Space::k100kHz;
  uint32_t freq_khz_ = 87500;
  uint8_t  volume_   = 8;
  bool     muted_    = false;
  bool     rds_enabled_ = false;

  RdsSnapshot rds_{};
  uint8_t     rds_text_ab_ = 0xFF;   // last seen A/B flag for RT segmentation.
};

}  // namespace drivers
