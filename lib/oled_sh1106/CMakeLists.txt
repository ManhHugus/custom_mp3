# Copyright 2026 mp3_esp32_fw contributors
# SPDX-License-Identifier: GPL-3.0-only
#
# ThingPulse SH1106/SSD1306 OLED driver, repackaged as an ESP-IDF component
# that uses a tiny Arduino/Wire shim (idf_shim/) instead of arduino-esp32.

idf_component_register(
  SRCS
    "src/OLEDDisplay.cpp"
    "src/OLEDDisplayUi.cpp"
    "idf_shim/Wire.cpp"
  INCLUDE_DIRS
    "src"
    "idf_shim"
  REQUIRES
    "driver"
    "esp_driver_i2c"
    "esp_timer"
    "freertos"
    "log"
)

# The upstream library is written for Arduino and produces a number of
# warnings under our strict EXTRA_WARNINGS profile. Disable them here so we
# don't have to patch the vendored sources.
# Force the Arduino code path in OLEDDisplay.h so it pulls in our shim
# Arduino.h instead of erroring out with "Unkown operating system".
target_compile_definitions(${COMPONENT_LIB} PUBLIC
  ARDUINO=200
)

target_compile_options(${COMPONENT_LIB} PRIVATE
  -Wno-narrowing
  -Wno-sign-compare
  -Wno-unused-parameter
  -Wno-unused-variable
  -Wno-unused-but-set-variable
  -Wno-missing-field-initializers
  -Wno-format
  -Wno-format-security
  -Wno-shift-negative-value
)
