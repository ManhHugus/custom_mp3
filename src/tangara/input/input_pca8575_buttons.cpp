/*
 * Copyright 2026 mp3_esp32_fw contributors
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "input/input_pca8575_buttons.hpp"

#include "drivers/pca8575.hpp"
#include "input/input_hook_actions.hpp"

namespace input {

using Pin = drivers::Pca8575::Pin;

Pca8575Buttons::Pca8575Buttons(drivers::Pca8575& pca,
                               audio::TrackQueue& queue)
    : pca_(pca),
      // click,                  double,                 long-press,            repeat
      ok_("ok",        actions::select(),         {},                       actions::togglePlayPause(), {}),
      back_("back",    actions::goBack(),         {},                       {},                         {}),
      left_("left",    actions::prevTrack(queue), {},                       actions::skipBack(),        {}),
      right_("right",  actions::nextTrack(queue), {},                       {},                         {}),
      center_("center",actions::scrollUp(),       {},                       actions::longPress(),       actions::scrollUp()),
      vol_up_("vol_up",   actions::volumeUp(),    {}, {}, actions::volumeUp()),
      vol_down_("vol_down", actions::volumeDown(), {}, {}, actions::volumeDown()) {}

auto Pca8575Buttons::read(lv_indev_data_t* data,
                          std::vector<InputEvent>& events) -> void {
  // Refresh the input cache from the chip. Ignore failures; we'll just keep
  // the previous snapshot.
  pca_.Read();

  // All buttons are active-low (external pull-up + quasi-bidi), so invert.
  const bool ok        = !pca_.Get(Pin::kBtnOk);
  const bool back      = !pca_.Get(Pin::kBtnBack);
  const bool left      = !pca_.Get(Pin::kBtnLeft);
  const bool right     = !pca_.Get(Pin::kBtnRight);
  const bool center    = !pca_.Get(Pin::kBtnCenter);
  const bool vol_up    = !pca_.Get(Pin::kBtnVolUp);
  const bool vol_down  = !pca_.Get(Pin::kBtnVolDown);

  ok_.update(ok, data);
  back_.update(back, data);
  left_.update(left, data);
  right_.update(right, data);
  center_.update(center, data);
  vol_up_.update(vol_up, data);
  vol_down_.update(vol_down, data);
}

auto Pca8575Buttons::name() -> std::string {
  return "pca8575_buttons";
}

auto Pca8575Buttons::triggers()
    -> std::vector<std::reference_wrapper<TriggerHooks>> {
  return {ok_, back_, left_, right_, center_, vol_up_, vol_down_};
}

}  // namespace input
