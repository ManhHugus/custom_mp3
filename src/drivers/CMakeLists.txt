# Copyright 2023 jacqueline <me@jacqueline.id.au>
#
# SPDX-License-Identifier: GPL-3.0-only

idf_component_register(
  SRCS "pca8575.cpp" "touchwheel.cpp" "i2s_dac.cpp" "gpios.cpp" "adc.cpp" "storage.cpp"
  "i2c.cpp" "bluetooth.cpp" "spi.cpp" "display.cpp" "display_init.cpp"
  "samd.cpp" "wm8523.cpp" "nvs.cpp" "haptics.cpp" "littlefs.cpp" "pcm_buffer.cpp"
  # --- mp3_esp32_fw stubs (parallel to the to-be-replaced upstream files) ---
  "sgtl5000.cpp" "sh1106.cpp" "rda5807.cpp"
  INCLUDE_DIRS "include"
  REQUIRES "esp_adc" "fatfs" "result" "lvgl" "nvs_flash" "esp_littlefs" "bt"
  "tasks" "tinyfsm" "util" "libcppbor" "driver" "esp_driver_i2c"
  "esp_driver_i2s" "esp_driver_spi" "esp_driver_sdmmc" "esp_driver_sdspi"
  "esp_driver_gpio" "esp_driver_ledc" "esp_ringbuf" "sdmmc" "vfs"
  "oled_sh1106")
target_compile_options(${COMPONENT_LIB} PRIVATE ${EXTRA_WARNINGS})
