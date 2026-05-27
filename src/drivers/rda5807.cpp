/*
 * Copyright 2026 mp3_esp32_fw contributors
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "drivers/rda5807.hpp"

#include <algorithm>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace drivers {

namespace {
constexpr const char* TAG = "rda5807";
constexpr uint32_t kClockHz = 100'000;  // chip is 400k-capable but be safe.

// Reset values copied from the datasheet so that the chip behaves
// identically whether or not it has been previously written.
constexpr uint16_t kReg02PowerUp = 0x0001;  // ENABLE only
constexpr uint16_t kReg03Default = 0x0000;  // BAND=US/EU, SPACE=100k
constexpr uint16_t kReg04Default = 0x0400;  // DE=50us bit (set per locale)
constexpr uint16_t kReg05Default = 0x888F;  // SEEKTH=8, max volume mask
constexpr uint16_t kReg06Default = 0x0000;
constexpr uint16_t kReg07Default = 0x0000;

// Helper: lookup table of band low edges in kHz (must match Band enum).
constexpr uint32_t kBandLow[]  = {87000, 76000, 76000, 65000};
constexpr uint32_t kBandHigh[] = {108000, 91000, 108000, 76000};
constexpr uint32_t kSpaceKHz[] = {100, 200, 50, 25};

inline uint16_t BeToHost(const uint8_t* p) {
  return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}
inline void HostToBe(uint16_t v, uint8_t* out) {
  out[0] = static_cast<uint8_t>(v >> 8);
  out[1] = static_cast<uint8_t>(v & 0xFF);
}
}  // namespace

auto Rda5807::Create(i2c_master_bus_handle_t bus) -> Rda5807* {
  if (bus == nullptr) return nullptr;

  i2c_device_config_t cfg_random = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address  = kAddrRandom,
      .scl_speed_hz    = kClockHz,
      .scl_wait_us     = 0,
      .flags           = {.disable_ack_check = false},
  };
  i2c_master_dev_handle_t random = nullptr;
  if (i2c_master_bus_add_device(bus, &cfg_random, &random) != ESP_OK) {
    ESP_LOGE(TAG, "failed to add device @0x11");
    return nullptr;
  }

  i2c_device_config_t cfg_seq = cfg_random;
  cfg_seq.device_address = kAddrSequential;
  i2c_master_dev_handle_t seq = nullptr;
  if (i2c_master_bus_add_device(bus, &cfg_seq, &seq) != ESP_OK) {
    ESP_LOGE(TAG, "failed to add device @0x10");
    i2c_master_bus_rm_device(random);
    return nullptr;
  }

  // Quick presence check: chip ID lives in register 0x00.  High byte
  // should be 0x58 on RDA5807-family parts.
  uint8_t reg = 0x00;
  uint8_t buf[2] = {0};
  esp_err_t err = i2c_master_transmit_receive(random, &reg, 1, buf, 2,
                                              pdMS_TO_TICKS(50));
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "no ack from RDA5807 (err=%d) - is the FM rail powered?",
             err);
    // Don't bail: the chip may still be coming up after a recent rail-on
    // event.  We'll retry inside PowerUp().
  } else {
    ESP_LOGI(TAG, "chip id = 0x%02X%02X", buf[0], buf[1]);
  }

  auto* dev = new Rda5807(bus, random, seq);
  if (!dev->PowerUp()) {
    ESP_LOGE(TAG, "PowerUp failed");
    delete dev;
    return nullptr;
  }
  return dev;
}

Rda5807::Rda5807(i2c_master_bus_handle_t bus,
                 i2c_master_dev_handle_t random,
                 i2c_master_dev_handle_t sequential)
    : bus_(bus), random_(random), sequential_(sequential) {
  shadow_[0x02] = kReg02PowerUp;
  shadow_[0x03] = kReg03Default;
  shadow_[0x04] = kReg04Default;
  shadow_[0x05] = kReg05Default;
  shadow_[0x06] = kReg06Default;
  shadow_[0x07] = kReg07Default;
}

Rda5807::~Rda5807() {
  PowerDown();
  if (random_)     i2c_master_bus_rm_device(random_);
  if (sequential_) i2c_master_bus_rm_device(sequential_);
}

// ---------------------------------------------------------------------------
// Low-level register I/O
// ---------------------------------------------------------------------------
auto Rda5807::WriteReg(uint8_t reg, uint16_t val) -> bool {
  uint8_t buf[3];
  buf[0] = reg;
  HostToBe(val, &buf[1]);
  esp_err_t err = i2c_master_transmit(random_, buf, 3, pdMS_TO_TICKS(50));
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "WriteReg(0x%02X)=0x%04X err=%d", reg, val, err);
    return false;
  }
  if (reg < shadow_.size()) shadow_[reg] = val;
  return true;
}

auto Rda5807::ReadReg(uint8_t reg, uint16_t& out) -> bool {
  uint8_t buf[2] = {0};
  esp_err_t err = i2c_master_transmit_receive(random_, &reg, 1, buf, 2,
                                              pdMS_TO_TICKS(50));
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "ReadReg(0x%02X) err=%d", reg, err);
    return false;
  }
  out = BeToHost(buf);
  return true;
}

auto Rda5807::ReadStatusBlock() -> bool {
  // Sequential read: chip starts at 0x0A and auto-increments.
  uint8_t raw[12] = {0};
  esp_err_t err = i2c_master_receive(sequential_, raw, sizeof(raw),
                                     pdMS_TO_TICKS(50));
  if (err != ESP_OK) return false;
  for (int i = 0; i < 6; ++i) {
    status_[i] = BeToHost(&raw[i * 2]);
  }
  return true;
}

auto Rda5807::ApplyShadow(uint8_t reg) -> bool {
  return WriteReg(reg, shadow_[reg]);
}

auto Rda5807::WaitForTuneComplete(uint32_t timeout_ms) -> bool {
  uint32_t waited = 0;
  while (waited < timeout_ms) {
    uint16_t r0a = 0;
    if (ReadReg(0x0A, r0a) && (r0a & 0x4000)) {  // STC bit (bit 14)
      status_[0] = r0a;
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    waited += 10;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Power
// ---------------------------------------------------------------------------
auto Rda5807::PowerUp() -> bool {
  // Reset all writeable registers to known defaults, then ENABLE.
  shadow_[0x02] = 0xC001;  // DHIZ | DMUTE | ENABLE  (bass off, mono off)
  shadow_[0x03] = static_cast<uint16_t>(static_cast<uint8_t>(band_) << 2)
                | static_cast<uint16_t>(static_cast<uint8_t>(space_));
  shadow_[0x04] = kReg04Default;
  shadow_[0x05] = (0x8 << 4)         // SEEKTH = 8
                | (volume_ & 0x0F)
                | (1u << 15);        // INT_MODE = 1
  shadow_[0x06] = 0;
  shadow_[0x07] = 0;
  for (uint8_t r = 0x02; r <= 0x07; ++r) {
    if (!ApplyShadow(r)) return false;
  }
  vTaskDelay(pdMS_TO_TICKS(60));  // datasheet: max 60ms after osc on.
  return true;
}

auto Rda5807::PowerDown() -> bool {
  shadow_[0x02] &= ~uint16_t{0x0001};  // clear ENABLE
  return ApplyShadow(0x02);
}

auto Rda5807::SoftReset() -> bool {
  uint16_t r = shadow_[0x02];
  WriteReg(0x02, r | 0x0002);
  vTaskDelay(pdMS_TO_TICKS(10));
  return WriteReg(0x02, r);
}

auto Rda5807::IsReady() -> bool {
  uint16_t r = 0;
  if (!ReadReg(0x0B, r)) return false;
  status_[1] = r;
  return (r & 0x0080) != 0;  // FM_READY (bit 7 of low byte).
}

// ---------------------------------------------------------------------------
// Tuning
// ---------------------------------------------------------------------------
auto Rda5807::BandLowKHz()  const -> uint32_t { return kBandLow [static_cast<uint8_t>(band_)]; }
auto Rda5807::BandHighKHz() const -> uint32_t { return kBandHigh[static_cast<uint8_t>(band_)]; }
auto Rda5807::SpaceKHz()    const -> uint32_t { return kSpaceKHz[static_cast<uint8_t>(space_)]; }

auto Rda5807::SetBand(Band b) -> bool {
  band_ = b;
  shadow_[0x03] = (shadow_[0x03] & ~uint16_t{0x000C})
                | static_cast<uint16_t>(static_cast<uint8_t>(b) << 2);
  return ApplyShadow(0x03);
}

auto Rda5807::SetSpace(Space s) -> bool {
  space_ = s;
  shadow_[0x03] = (shadow_[0x03] & ~uint16_t{0x0003})
                | static_cast<uint16_t>(static_cast<uint8_t>(s));
  return ApplyShadow(0x03);
}

auto Rda5807::SetFrequency(uint32_t khz) -> bool {
  uint32_t lo = BandLowKHz();
  uint32_t hi = BandHighKHz();
  if (khz < lo) khz = lo;
  if (khz > hi) khz = hi;
  uint32_t step = SpaceKHz();
  uint32_t channel = (khz - lo) / step;
  if (channel > 0x3FF) channel = 0x3FF;

  uint16_t r03 = static_cast<uint16_t>(channel << 6)
               | static_cast<uint16_t>(1u << 4)            // TUNE
               | static_cast<uint16_t>(static_cast<uint8_t>(band_) << 2)
               | static_cast<uint16_t>(static_cast<uint8_t>(space_));
  shadow_[0x03] = r03;
  if (!ApplyShadow(0x03)) return false;
  WaitForTuneComplete();
  freq_khz_ = lo + channel * step;
  return true;
}

auto Rda5807::FrequencyUp() -> bool {
  uint32_t step = SpaceKHz();
  uint32_t f = freq_khz_ + step;
  if (f > BandHighKHz()) f = BandLowKHz();
  return SetFrequency(f);
}

auto Rda5807::FrequencyDown() -> bool {
  uint32_t step = SpaceKHz();
  uint32_t f = (freq_khz_ > BandLowKHz() + step) ? freq_khz_ - step
                                                  : BandHighKHz();
  return SetFrequency(f);
}

// ---------------------------------------------------------------------------
// Seek
// ---------------------------------------------------------------------------
auto Rda5807::StartSeek(SeekDir dir, bool wrap) -> bool {
  uint16_t r02 = shadow_[0x02];
  r02 |= (1u << 8);                                          // SEEK
  if (wrap)
    r02 &= ~uint16_t{1u << 7};                               // SKMODE=0 wrap
  else
    r02 |= (1u << 7);
  if (dir == SeekDir::kUp)
    r02 |= (1u << 9);                                        // SEEKUP
  else
    r02 &= ~uint16_t{1u << 9};
  shadow_[0x02] = r02;
  return ApplyShadow(0x02);
}

auto Rda5807::IsSeekComplete() -> bool {
  uint16_t r0a = 0;
  if (!ReadReg(0x0A, r0a)) return false;
  status_[0] = r0a;
  return (r0a & 0x4000) != 0;  // STC
}

auto Rda5807::FinishSeek() -> bool {
  uint16_t r0a = status_[0];
  // Clear SEEK bit.
  shadow_[0x02] &= ~uint16_t{1u << 8};
  ApplyShadow(0x02);
  // Refresh frequency from real channel.
  uint16_t channel = r0a & 0x03FF;
  freq_khz_ = BandLowKHz() + channel * SpaceKHz();
  bool sf = (r0a & 0x2000) != 0;  // seek fail
  return !sf;
}

auto Rda5807::SetSeekThreshold(uint8_t snr) -> bool {
  if (snr > 0x0F) snr = 0x0F;
  shadow_[0x05] = (shadow_[0x05] & ~uint16_t{0x00F0})
                | static_cast<uint16_t>(snr << 4);
  return ApplyShadow(0x05);
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
auto Rda5807::SetMute(bool m) -> bool {
  muted_ = m;
  if (m) shadow_[0x02] &= ~uint16_t{0x4000};   // DMUTE=0 -> mute
  else   shadow_[0x02] |=  uint16_t{0x4000};
  return ApplyShadow(0x02);
}

auto Rda5807::SetMono(bool mono) -> bool {
  if (mono) shadow_[0x02] |= uint16_t{0x2000};
  else      shadow_[0x02] &= ~uint16_t{0x2000};
  return ApplyShadow(0x02);
}

auto Rda5807::SetBass(bool on) -> bool {
  if (on) shadow_[0x02] |= uint16_t{0x1000};
  else    shadow_[0x02] &= ~uint16_t{0x1000};
  return ApplyShadow(0x02);
}

auto Rda5807::SetVolume(uint8_t v) -> bool {
  if (v > 15) v = 15;
  volume_ = v;
  shadow_[0x05] = (shadow_[0x05] & ~uint16_t{0x000F}) | v;
  return ApplyShadow(0x05);
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
auto Rda5807::GetRssi() -> uint8_t {
  uint16_t r = 0;
  if (!ReadReg(0x0B, r)) return 0;
  status_[1] = r;
  return static_cast<uint8_t>((r >> 9) & 0x7F);
}

auto Rda5807::IsStereo() -> bool {
  uint16_t r = 0;
  if (!ReadReg(0x0A, r)) return false;
  status_[0] = r;
  return (r & 0x0400) != 0;  // ST bit (bit 10)
}

auto Rda5807::IsTuned() -> bool {
  uint16_t r = 0;
  if (!ReadReg(0x0B, r)) return false;
  status_[1] = r;
  return (r & 0x0100) != 0;  // FM_TRUE (bit 8 of low byte)
}

// ---------------------------------------------------------------------------
// RDS
// ---------------------------------------------------------------------------
auto Rda5807::SetRds(bool enable) -> bool {
  rds_enabled_ = enable;
  if (enable) shadow_[0x02] |= uint16_t{0x0008};   // RDS_EN
  else        shadow_[0x02] &= ~uint16_t{0x0008};
  return ApplyShadow(0x02);
}

auto Rda5807::ClearRds() -> void {
  rds_ = {};
  rds_text_ab_ = 0xFF;
}

auto Rda5807::PollRds() -> bool {
  if (!rds_enabled_) return false;
  if (!ReadStatusBlock()) return false;
  bool rdsr = (status_[0] & 0x8000) != 0;  // RDSR (bit 15)
  bool rdss = (status_[0] & 0x1000) != 0;  // RDSS sync
  rds_.synced = rdss;
  if (!rdsr) return false;
  return DecodeRdsGroup();
}

auto Rda5807::DecodeRdsGroup() -> bool {
  uint16_t blockA = status_[2];  // 0x0C - PI code
  uint16_t blockB = status_[3];  // 0x0D
  uint16_t blockC = status_[4];  // 0x0E
  uint16_t blockD = status_[5];  // 0x0F

  rds_.pi = blockA;

  uint8_t group_type = (blockB >> 12) & 0x0F;
  uint8_t version    = (blockB >> 11) & 0x01;
  uint8_t pty        = (blockB >> 5)  & 0x1F;
  rds_.pty = pty;
  rds_.has_pty = pty != 0;

  bool changed = false;

  if (group_type == 0) {
    // Group 0A/0B: PS (programme service name).  blockD carries 2 chars,
    // address from low 2 bits of blockB.
    uint8_t addr = blockB & 0x03;
    char c1 = static_cast<char>((blockD >> 8) & 0xFF);
    char c2 = static_cast<char>(blockD & 0xFF);
    if (c1 < 0x20) c1 = ' ';
    if (c2 < 0x20) c2 = ' ';
    if (rds_.ps[addr * 2]     != c1 ||
        rds_.ps[addr * 2 + 1] != c2) {
      rds_.ps[addr * 2]     = c1;
      rds_.ps[addr * 2 + 1] = c2;
      rds_.ps[8] = '\0';
      changed = true;
    }
    rds_.has_ps = true;
  } else if (group_type == 2) {
    // Group 2A/2B: RT (radio text). Reset buffer if A/B flag toggles.
    uint8_t ab = (blockB >> 4) & 0x01;
    if (ab != rds_text_ab_) {
      std::memset(rds_.rt, 0, sizeof(rds_.rt));
      rds_text_ab_ = ab;
      changed = true;
    }
    uint8_t addr = blockB & 0x0F;
    if (version == 0) {
      // 2A: 4 chars per group at addr*4
      char buf[4] = {
          static_cast<char>((blockC >> 8) & 0xFF),
          static_cast<char>(blockC & 0xFF),
          static_cast<char>((blockD >> 8) & 0xFF),
          static_cast<char>(blockD & 0xFF),
      };
      for (int i = 0; i < 4; ++i) {
        size_t pos = addr * 4 + i;
        if (pos >= 64) break;
        char c = buf[i];
        if (c == 0x0D) c = '\0';   // RDS terminator -> end of string.
        if (c != 0 && c < 0x20) c = ' ';
        if (rds_.rt[pos] != c) {
          rds_.rt[pos] = c;
          changed = true;
        }
      }
    } else {
      // 2B: 2 chars per group at addr*2 (in blockD).
      char buf[2] = {
          static_cast<char>((blockD >> 8) & 0xFF),
          static_cast<char>(blockD & 0xFF),
      };
      for (int i = 0; i < 2; ++i) {
        size_t pos = addr * 2 + i;
        if (pos >= 64) break;
        char c = buf[i];
        if (c == 0x0D) c = '\0';
        if (c != 0 && c < 0x20) c = ' ';
        if (rds_.rt[pos] != c) {
          rds_.rt[pos] = c;
          changed = true;
        }
      }
    }
    rds_.rt[64] = '\0';
  }

  return changed;
}

}  // namespace drivers

