/*
 * Copyright 2026 mp3_esp32_fw contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Tiny Arduino API shim so the ThingPulse OLED library can build under
 * pure ESP-IDF (no arduino-esp32 component). Only declares the subset used
 * by OLEDDisplay.cpp / OLEDDisplayUi.cpp / SH1106Wire.h.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#ifdef __cplusplus
#include <algorithm>
using std::min;
using std::max;
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#ifndef ARDUINO
#define ARDUINO 200
#endif

// PROGMEM / pgm_read_byte: ESP32 has unified address space, just deref.
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#endif

// Time helpers.
static inline unsigned long millis(void) {
  return (unsigned long)(esp_timer_get_time() / 1000);
}
static inline void delay(unsigned long ms) {
  vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
}
static inline void yield(void) { taskYIELD(); }

// Arduino min/max helpers used by the OLED library.
#ifndef _min
#define _min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef _max
#define _max(a, b) ((a) > (b) ? (a) : (b))
#endif

typedef uint8_t byte;

#ifdef __cplusplus

#include <string>

// Minimal `String` shim — only the members used by OLEDDisplay are provided.
class String {
 public:
  String() {}
  String(const char* s) : str_(s ? s : "") {}
  String(const std::string& s) : str_(s) {}
  const char* c_str() const { return str_.c_str(); }
  unsigned int length() const { return (unsigned int)str_.size(); }
  void toCharArray(char* buf, unsigned int bufsize, unsigned int index = 0) const {
    if (!buf || bufsize == 0) return;
    unsigned int len = (unsigned int)str_.size();
    if (index >= len) { buf[0] = '\0'; return; }
    unsigned int n = len - index;
    if (n > bufsize - 1) n = bufsize - 1;
    memcpy(buf, str_.data() + index, n);
    buf[n] = '\0';
  }
 private:
  std::string str_;
};

// Minimal `Print` base — OLEDDisplay derives from this but the project never
// invokes Print's text formatting helpers (it uses drawString directly), so
// the bare minimum surface is enough.
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t c) = 0;
  virtual size_t writeBuffer(const uint8_t* buf, size_t n) {
    size_t i = 0;
    while (i < n) { write(buf[i++]); }
    return n;
  }
  size_t write(const char* s) {
    return s ? writeBuffer(reinterpret_cast<const uint8_t*>(s), strlen(s)) : 0;
  }
};

#endif  // __cplusplus
