/*
 * Copyright 2026 mp3_esp32_fw contributors
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Input device backed by the PCA8575 GPIO expander.
 *
 * Polled by LVGL each tick. Maps the seven physical buttons on the new board
 * (OK / BACK / LEFT / RIGHT / CENTER / VOL_UP / VOL_DOWN) to navigation,
 * playback and volume actions.
 */
#pragma once

#include <cstdint>

#include "audio/track_queue.hpp"
#include "indev/lv_indev.h"

#include "drivers/pca8575.hpp"
#include "input/input_device.hpp"
#include "input/input_hook.hpp"

namespace input {

class Pca8575Buttons : public IInputDevice {
 public:
  Pca8575Buttons(drivers::Pca8575& pca, audio::TrackQueue& queue);

  auto read(lv_indev_data_t* data, std::vector<InputEvent>& events)
      -> void override;

  auto name() -> std::string override;
  auto triggers()
      -> std::vector<std::reference_wrapper<TriggerHooks>> override;

 private:
  drivers::Pca8575& pca_;

  TriggerHooks ok_;
  TriggerHooks back_;
  TriggerHooks left_;
  TriggerHooks right_;
  TriggerHooks center_;
  TriggerHooks vol_up_;
  TriggerHooks vol_down_;
};

}  // namespace input
