/*
 * Copyright 2026 mp3_esp32_fw contributors
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * SGTL5000 stereo audio codec — I2C control + I2S audio.
 *
 * This is a stub. The corresponding .cpp body is not yet implemented; only
 * the interface and register map are sketched so that callers (notably
 * `i2s_audio_output.cpp`, which currently talks to the WM8523) can be
 * incrementally migrated.
 *
 * Volume convention here mirrors `drivers::wm8523` so the audio pipeline can
 * be reused without rewriting its volume curve:
 *   - `kAbsoluteMaxVolume` ... `kAbsoluteMinVolume` define the raw range.
 *   - `VolumeToDb` / `DbToVolume` convert at the same 0.25 dB/step granularity
 *     so existing UI code Just Works.
 *
 * SGTL5000 internal architecture (simplified):
 *     I2S_IN -> DAC -> HP_OUT  (independent volume: CHIP_ANA_HP_CTRL)
 *                  \-> LINE_OUT (CHIP_LINE_OUT_VOL)
 *     I2S_IN -> [DAP block, optional EQ/treble] -> DAC
 *
 * Reference: SGTL5000 datasheet (NXP/Freescale), revision 5.
 */
#pragma once

#include <cstdint>
#include <optional>

#include "esp_err.h"

// SGTL5000-specific defines for headphones
#define AUDIO_HEADPHONE_DAC 0
#define AUDIO_HEADPHONE_LINEIN 1
#define AUDIO_INPUT_LINEIN  0
#define AUDIO_INPUT_MIC     1
#define AUDIO_SAMPLE_RATE_EXACT 44100.0f

namespace drivers {
    namespace sgtl5000 {

        // --- Volume range, picked to mirror drivers::wm8523 semantics ---------------
        // (4 raw steps per dB; line-level reference is the codec's "0 dB attenuation"
        // point on the headphone amp.) Adjust once real curves are characterised.
        extern const uint16_t kAbsoluteMaxVolume;
        extern const uint16_t kAbsoluteMinVolume;
        extern const uint16_t kMaxVolumeBeforeClipping;
        extern const uint16_t kLineLevelReferenceVolume;
        extern const uint16_t kDefaultVolume;;
        extern const uint16_t kDefaultMaxVolume;
        extern const uint16_t kZeroDbVolume;

        constexpr auto VolumeToDb(uint16_t vol) -> int_fast8_t {
        return (vol - kLineLevelReferenceVolume) / 4;
        }

        constexpr auto DbToVolume(int_fast8_t db) -> uint16_t {
        return (db * 4) + kLineLevelReferenceVolume;
        }

        // Convenience wrappers (TODO: implement).
        auto SetHeadphoneVolume(uint16_t vol) -> bool;
        auto SetMuted(bool mute) -> bool;

        void setAddress(uint8_t level);
        bool enable(void);//For Teensy LC the SGTL acts as master, for all other Teensys as slave.
        bool enable(const unsigned extMCLK, const uint32_t pllFreq = (4096.0l * AUDIO_SAMPLE_RATE_EXACT) ); //With extMCLK > 0, the SGTL acts as Master
        bool disable(void) { return false; }
        bool volume(float n);
        bool inputLevel(float n) {return false;}
        bool muteHeadphone(void);
        bool unmuteHeadphone(void);
        bool muteLineout(void);
        bool unmuteLineout(void);
        bool inputSelect(int n);
        bool headphoneSelect(int n);
        bool volume(float left, float right);
        bool micGain(unsigned int dB);
        bool lineInLevel(uint8_t n);
        bool lineInLevel(uint8_t left, uint8_t right);
        uint16_t lineOutLevel(uint8_t n);
        uint16_t lineOutLevel(uint8_t left, uint8_t right);
        uint16_t dacVolume(float n);
        uint16_t dacVolume(float left, float right);
        bool dacVolumeRamp();
        bool dacVolumeRampLinear();
        bool dacVolumeRampDisable();
        uint16_t adcHighPassFilterEnable(void);
        uint16_t adcHighPassFilterFreeze(void);
        uint16_t adcHighPassFilterDisable(void);
        uint16_t audioPreProcessorEnable(void);
        uint16_t audioPostProcessorEnable(void);
        uint16_t audioProcessorDisable(void);
        uint16_t eqFilterCount(uint8_t n);
        uint16_t eqSelect(uint8_t n);
        uint16_t eqBand(uint8_t bandNum, float n);
        void eqBands(float bass, float mid_bass, float midrange, float mid_treble, float treble);
        void eqBands(float bass, float treble);
        void eqFilter(uint8_t filterNum, int *filterParameters);
        uint16_t autoVolumeControl(uint8_t maxGain, uint8_t lbiResponse, uint8_t hardLimit, float threshold, float attack, float decay);
        uint16_t autoVolumeEnable(void);
        uint16_t autoVolumeDisable(void);
        uint16_t enhanceBass(float lr_lev, float bass_lev);
        uint16_t enhanceBass(float lr_lev, float bass_lev, uint8_t hpf_bypass, uint8_t cutoff);
        uint16_t enhanceBassEnable(void);
        uint16_t enhanceBassDisable(void);
        uint16_t surroundSound(uint8_t width);
        uint16_t surroundSound(uint8_t width, uint8_t select);
        uint16_t surroundSoundEnable(void);
        uint16_t surroundSoundDisable(void);
        void killAutomation(void);
        void setMasterMode(uint32_t freqMCLK_in);

    }  // namespace sgtl5000
}  // namespace drivers

//For Filter Type: 0 = LPF, 1 = HPF, 2 = BPF, 3 = NOTCH, 4 = PeakingEQ, 5 = LowShelf, 6 = HighShelf
#define FILTER_LOPASS 0
#define FILTER_HIPASS 1
#define FILTER_BANDPASS 2
#define FILTER_NOTCH 3
#define FILTER_PARAEQ 4
#define FILTER_LOSHELF 5
#define FILTER_HISHELF 6

//For frequency adjustment
#define FLAT_FREQUENCY 0
#define PARAMETRIC_EQUALIZER 1
#define TONE_CONTROLS 2
#define GRAPHIC_EQUALIZER 3


void calcBiquad(uint8_t filtertype, float fC, float dB_Gain, float Q, uint32_t quantization_unit, uint32_t fS, int *coef);
