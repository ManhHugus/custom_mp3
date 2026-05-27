/*
 * Copyright 2023 jacqueline <me@jacqueline.id.au>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "input/device_factory.hpp"

#include <memory>

#include "input/feedback_haptics.hpp"
#include "input/feedback_tts.hpp"
#include "input/input_device.hpp"
#include "input/input_hard_reset.hpp"
#include "input/input_lua.hpp"
#include "input/input_media_buttons.hpp"
#include "input/input_nav_buttons.hpp"
#include "input/input_pca8575_buttons.hpp"
#include "input/input_touch_dpad.hpp"
#include "input/input_touch_wheel.hpp"
#include "input/input_volume_buttons.hpp"
#include "lua/lua_registry.hpp"

namespace input {

DeviceFactory::DeviceFactory(
    std::shared_ptr<system_fsm::ServiceLocator> services)
    : services_(services) {
  if (services->touchwheel()) {
    wheel_ = std::make_shared<TouchWheel>(
        services->nvs(), **services->touchwheel(), services->track_queue());
    auto wheel_mode = services_->nvs().WheelInput();
    if (wheel_mode == drivers::NvsStorage::WheelInputModes::kWheelWithButtons) {
      wheel_->activate_buttons(true);
    }
  }
  lua_input_ = std::make_shared<LuaInput>(
      lua::Registry::instance(*services_).uiThread());
  reset_ = std::make_shared<HardReset>(services_->gpios());
}

auto DeviceFactory::createLockedInputs()
    -> std::vector<std::shared_ptr<IInputDevice>> {
  // The new board has no lock switch — see Gpios::IsLocked(). This path is
  // therefore never taken in practice, but keep it valid for completeness.
  std::vector<std::shared_ptr<IInputDevice>> ret;
  if (services_->pca8575()) {
    ret.push_back(std::make_shared<Pca8575Buttons>(*services_->pca8575(),
                                                   services_->track_queue()));
  }
  ret.push_back(reset_);
  return ret;
}

auto DeviceFactory::createInputs()
    -> std::vector<std::shared_ptr<IInputDevice>> {
  std::vector<std::shared_ptr<IInputDevice>> ret;

  // Touch wheel is no longer present on this board, but keep the optional
  // path alive so that future variants / unit tests can plug one in.
  auto wheel_mode = services_->nvs().WheelInput();
  switch (wheel_mode) {
    case drivers::NvsStorage::WheelInputModes::kDirectionalWheel:
      if (services_->touchwheel()) {
        ret.push_back(std::make_shared<TouchDPad>(**services_->touchwheel()));
      }
      break;
    case drivers::NvsStorage::WheelInputModes::kRotatingWheel:
    case drivers::NvsStorage::WheelInputModes::kWheelWithButtons:
      if (wheel_) {
        wheel_->activate_buttons(
            wheel_mode ==
            drivers::NvsStorage::WheelInputModes::kWheelWithButtons);
        ret.push_back(wheel_);
      }
      break;
    case drivers::NvsStorage::WheelInputModes::kDisabled:
    default:
      break;
  }

  // Real buttons live on the PCA8575 expander on this board.
  if (services_->pca8575()) {
    ret.push_back(std::make_shared<Pca8575Buttons>(*services_->pca8575(),
                                                   services_->track_queue()));
  }

  ret.push_back(reset_);
  ret.push_back(lua_input_);
  return ret;
}

auto DeviceFactory::createFeedbacks()
    -> std::vector<std::shared_ptr<IFeedbackDevice>> {
  std::vector<std::shared_ptr<IFeedbackDevice>> ret;
  if (services_->haptics()) {
    ret.push_back(std::make_shared<Haptics>(**services_->haptics(), services_));
  }
  ret.push_back(std::make_shared<TextToSpeech>(services_->tts()));
  return ret;
}

}  // namespace input
