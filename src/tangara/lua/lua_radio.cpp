/*
 * Copyright 2026 mp3_esp32_fw contributors
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Lua bindings for the RDA5807FP FM tuner.
 *
 * Module exposed as `radio`.  All functions silently return nil/false when
 * the chip is absent (e.g. FM rail off, hardware fault).
 *
 *   radio.tune(khz)              -> integer kHz  (actual tuned freq)
 *   radio.frequency()            -> integer kHz
 *   radio.up() / radio.down()    -> integer kHz
 *   radio.set_band(b)            -- 0..3
 *   radio.set_space(s)           -- 0..3 (100/200/50/25 kHz)
 *   radio.seek(direction, wrap)  -- direction "up"|"down", returns true=ok
 *   radio.set_volume(v)          -- 0..15
 *   radio.volume()               -> integer
 *   radio.set_mute(b)
 *   radio.set_mono(b)
 *   radio.set_bass(b)
 *   radio.set_rds(b)
 *   radio.poll()                 -- pump RDS; returns true if changed
 *   radio.station_name()         -> string (8 chars, may have spaces)
 *   radio.radio_text()           -> string (up to 64 chars)
 *   radio.rssi()                 -> integer 0..63
 *   radio.is_stereo()            -> bool
 *   radio.is_tuned()             -> bool
 *   radio.preset_save(slot)      -- slot 1..16, persists current freq
 *   radio.preset_recall(slot)    -- tunes saved freq
 *   radio.preset_clear(slot)
 *   radio.presets()              -> table[slot]=khz of all saved presets
 */

#include "lua/lua_radio.hpp"

#include <array>
#include <cstring>
#include <string>

#include "esp_log.h"
#include "lauxlib.h"
#include "lua.h"
#include "lua.hpp"
#include "nvs.h"
#include "nvs_flash.h"

#include "drivers/rda5807.hpp"
#include "lua/bridge.hpp"
#include "system_fsm/service_locator.hpp"

namespace lua {

namespace {

constexpr const char* kTag = "lua_radio";
constexpr const char* kNvsNamespace = "radio";
constexpr const char* kNvsPresetsKey = "presets";  // blob: 16x uint16 (kHz/10)
constexpr const char* kNvsLastFreqKey = "last";    // u32 kHz
constexpr int kPresetSlots = 16;

inline auto Radio(lua_State* L) -> drivers::Rda5807* {
  return Bridge::Get(L)->services().radio();
}

inline auto LoadPresets() -> std::array<uint32_t, kPresetSlots> {
  std::array<uint32_t, kPresetSlots> out{};
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return out;
  size_t sz = sizeof(out);
  nvs_get_blob(h, kNvsPresetsKey, out.data(), &sz);
  nvs_close(h);
  return out;
}

inline auto SavePresets(const std::array<uint32_t, kPresetSlots>& p) -> bool {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_set_blob(h, kNvsPresetsKey, p.data(), sizeof(p));
  if (err == ESP_OK) nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

inline auto SaveLastFreq(uint32_t khz) -> void {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_u32(h, kNvsLastFreqKey, khz);
  nvs_commit(h);
  nvs_close(h);
}

// -- bindings ---------------------------------------------------------------

static auto l_tune(lua_State* L) -> int {
  auto* r = Radio(L);
  if (!r) { lua_pushnil(L); return 1; }
  uint32_t khz = static_cast<uint32_t>(luaL_checkinteger(L, 1));
  r->SetFrequency(khz);
  SaveLastFreq(r->GetFrequency());
  lua_pushinteger(L, r->GetFrequency());
  return 1;
}

static auto l_frequency(lua_State* L) -> int {
  auto* r = Radio(L);
  if (!r) { lua_pushnil(L); return 1; }
  lua_pushinteger(L, r->GetFrequency());
  return 1;
}

static auto l_up(lua_State* L) -> int {
  auto* r = Radio(L);
  if (!r) { lua_pushnil(L); return 1; }
  r->FrequencyUp();
  SaveLastFreq(r->GetFrequency());
  lua_pushinteger(L, r->GetFrequency());
  return 1;
}

static auto l_down(lua_State* L) -> int {
  auto* r = Radio(L);
  if (!r) { lua_pushnil(L); return 1; }
  r->FrequencyDown();
  SaveLastFreq(r->GetFrequency());
  lua_pushinteger(L, r->GetFrequency());
  return 1;
}

static auto l_set_band(lua_State* L) -> int {
  auto* r = Radio(L);
  if (!r) return 0;
  int b = luaL_checkinteger(L, 1);
  if (b < 0 || b > 3) return luaL_error(L, "band out of range");
  r->SetBand(static_cast<drivers::Rda5807::Band>(b));
  return 0;
}

static auto l_set_space(lua_State* L) -> int {
  auto* r = Radio(L);
  if (!r) return 0;
  int s = luaL_checkinteger(L, 1);
  if (s < 0 || s > 3) return luaL_error(L, "space out of range");
  r->SetSpace(static_cast<drivers::Rda5807::Space>(s));
  return 0;
}

static auto l_seek(lua_State* L) -> int {
  auto* r = Radio(L);
  if (!r) { lua_pushboolean(L, false); return 1; }
  const char* dir = luaL_optstring(L, 1, "up");
  bool wrap = lua_isnoneornil(L, 2) ? true : lua_toboolean(L, 2);
  bool up = std::strcmp(dir, "up") == 0;
  r->StartSeek(up ? drivers::Rda5807::SeekDir::kUp
                  : drivers::Rda5807::SeekDir::kDown,
               wrap);
  // Block up to ~3 s polling for STC; UI script can also call seek
  // asynchronously by polling radio.frequency() afterwards.
  for (int i = 0; i < 300; ++i) {
    if (r->IsSeekComplete()) break;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  bool ok = r->FinishSeek();
  SaveLastFreq(r->GetFrequency());
  lua_pushboolean(L, ok);
  lua_pushinteger(L, r->GetFrequency());
  return 2;
}

static auto l_set_volume(lua_State* L) -> int {
  auto* r = Radio(L);
  if (!r) return 0;
  int v = luaL_checkinteger(L, 1);
  if (v < 0) v = 0;
  if (v > 15) v = 15;
  r->SetVolume(static_cast<uint8_t>(v));
  return 0;
}

static auto l_volume(lua_State* L) -> int {
  auto* r = Radio(L);
  if (!r) { lua_pushnil(L); return 1; }
  lua_pushinteger(L, r->GetVolume());
  return 1;
}

static auto l_set_mute(lua_State* L) -> int {
  auto* r = Radio(L);
  if (r) r->SetMute(lua_toboolean(L, 1));
  return 0;
}
static auto l_set_mono(lua_State* L) -> int {
  auto* r = Radio(L);
  if (r) r->SetMono(lua_toboolean(L, 1));
  return 0;
}
static auto l_set_bass(lua_State* L) -> int {
  auto* r = Radio(L);
  if (r) r->SetBass(lua_toboolean(L, 1));
  return 0;
}
static auto l_set_rds(lua_State* L) -> int {
  auto* r = Radio(L);
  if (r) r->SetRds(lua_toboolean(L, 1));
  return 0;
}

static auto l_poll(lua_State* L) -> int {
  auto* r = Radio(L);
  lua_pushboolean(L, r ? r->PollRds() : 0);
  return 1;
}

static auto l_station_name(lua_State* L) -> int {
  auto* r = Radio(L);
  if (!r) { lua_pushliteral(L, ""); return 1; }
  auto snap = r->GetRds();
  lua_pushstring(L, snap.ps);
  return 1;
}

static auto l_radio_text(lua_State* L) -> int {
  auto* r = Radio(L);
  if (!r) { lua_pushliteral(L, ""); return 1; }
  auto snap = r->GetRds();
  lua_pushstring(L, snap.rt);
  return 1;
}

static auto l_rssi(lua_State* L) -> int {
  auto* r = Radio(L);
  lua_pushinteger(L, r ? r->GetRssi() : 0);
  return 1;
}

static auto l_is_stereo(lua_State* L) -> int {
  auto* r = Radio(L);
  lua_pushboolean(L, r ? r->IsStereo() : 0);
  return 1;
}

static auto l_is_tuned(lua_State* L) -> int {
  auto* r = Radio(L);
  lua_pushboolean(L, r ? r->IsTuned() : 0);
  return 1;
}

// -- presets ----------------------------------------------------------------

static auto l_preset_save(lua_State* L) -> int {
  auto* r = Radio(L);
  if (!r) { lua_pushboolean(L, false); return 1; }
  int slot = luaL_checkinteger(L, 1);
  if (slot < 1 || slot > kPresetSlots)
    return luaL_error(L, "preset slot out of range (1..%d)", kPresetSlots);
  auto presets = LoadPresets();
  presets[slot - 1] = r->GetFrequency();
  lua_pushboolean(L, SavePresets(presets));
  return 1;
}

static auto l_preset_recall(lua_State* L) -> int {
  auto* r = Radio(L);
  if (!r) { lua_pushnil(L); return 1; }
  int slot = luaL_checkinteger(L, 1);
  if (slot < 1 || slot > kPresetSlots)
    return luaL_error(L, "preset slot out of range");
  auto presets = LoadPresets();
  uint32_t khz = presets[slot - 1];
  if (khz == 0) { lua_pushnil(L); return 1; }
  r->SetFrequency(khz);
  SaveLastFreq(r->GetFrequency());
  lua_pushinteger(L, r->GetFrequency());
  return 1;
}

static auto l_preset_clear(lua_State* L) -> int {
  int slot = luaL_checkinteger(L, 1);
  if (slot < 1 || slot > kPresetSlots)
    return luaL_error(L, "preset slot out of range");
  auto presets = LoadPresets();
  presets[slot - 1] = 0;
  lua_pushboolean(L, SavePresets(presets));
  return 1;
}

static auto l_presets(lua_State* L) -> int {
  auto presets = LoadPresets();
  lua_createtable(L, kPresetSlots, 0);
  for (int i = 0; i < kPresetSlots; ++i) {
    if (presets[i] != 0) {
      lua_pushinteger(L, presets[i]);
      lua_rawseti(L, -2, i + 1);
    }
  }
  return 1;
}

static const luaL_Reg kRadioFuncs[] = {
    {"tune",          l_tune},
    {"frequency",     l_frequency},
    {"up",            l_up},
    {"down",          l_down},
    {"set_band",      l_set_band},
    {"set_space",     l_set_space},
    {"seek",          l_seek},
    {"set_volume",    l_set_volume},
    {"volume",        l_volume},
    {"set_mute",      l_set_mute},
    {"set_mono",      l_set_mono},
    {"set_bass",      l_set_bass},
    {"set_rds",       l_set_rds},
    {"poll",          l_poll},
    {"station_name",  l_station_name},
    {"radio_text",    l_radio_text},
    {"rssi",          l_rssi},
    {"is_stereo",     l_is_stereo},
    {"is_tuned",      l_is_tuned},
    {"preset_save",   l_preset_save},
    {"preset_recall", l_preset_recall},
    {"preset_clear",  l_preset_clear},
    {"presets",       l_presets},
    {nullptr, nullptr},
};

static auto luaopen_radio(lua_State* L) -> int {
  luaL_newlib(L, kRadioFuncs);
  return 1;
}

}  // namespace

auto RegisterRadioModule(lua_State* L) -> void {
  luaL_requiref(L, "radio", luaopen_radio, true);
  lua_pop(L, 1);
}

}  // namespace lua
