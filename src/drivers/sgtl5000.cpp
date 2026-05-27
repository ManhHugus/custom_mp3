/*
 * Copyright 2026 mp3_esp32_fw contributors
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * SGTL5000 stub — register accessors over the shared I2C bus.
 *
 * NOTE: this file is intentionally minimal. The intended migration path is:
 *   1. Wire up I2C scan in BSP init, confirm 0x0A (or 0x2A) ACKs.
 *   2. Implement ReadRegister/WriteRegister against drivers::i2c_master_dev().
 *   3. Flesh out Init() following the datasheet power-up sequence (§4.10):
 *        VDDD ramp -> CHIP_ANA_POWER -> CHIP_REF_CTRL -> CHIP_LINE_OUT_CTRL
 *        -> CHIP_SHORT_CTRL -> CHIP_ANA_POWER (enable analog) -> CHIP_DIG_POWER
 *        -> CHIP_CLK_CTRL / CHIP_I2S_CTRL -> route DAC -> unmute.
 *   4. Replace `drivers::wm8523::Init()` call in
 *      `src/tangara/system_fsm/booting.cpp` with `drivers::sgtl5000::Init()`.
 *   5. Replace volume calls in `src/tangara/audio/i2s_audio_output.cpp`.
 */
#include "drivers/sgtl5000.hpp"

#include "esp_err.h"
#include "esp_log.h"
#include "drivers/i2c.hpp"
#include <cmath>

#define LOW 0
#define HIGH 1

#define CHIP_ID				0x0000
#define CHIP_DIG_POWER			0x0002
#define CHIP_CLK_CTRL			0x0004
#define CHIP_I2S_CTRL			0x0006
#define CHIP_SSS_CTRL			0x000A
#define CHIP_ADCDAC_CTRL		0x000E
#define CHIP_DAC_VOL			0x0010
#define CHIP_PAD_STRENGTH		0x0014
#define CHIP_ANA_ADC_CTRL		0x0020
#define CHIP_ANA_HP_CTRL		0x0022
#define CHIP_ANA_CTRL			0x0024
#define CHIP_LINREG_CTRL		0x0026
#define CHIP_REF_CTRL			0x0028 // bandgap reference bias voltage and currents
#define CHIP_MIC_CTRL			0x002A // microphone gain & internal microphone bias
#define CHIP_LINE_OUT_CTRL		0x002C
#define CHIP_LINE_OUT_VOL		0x002E
#define CHIP_ANA_POWER			0x0030 // power down controls for the analog blocks.
#define CHIP_PLL_CTRL			0x0032
#define CHIP_CLK_TOP_CTRL		0x0034
#define CHIP_ANA_STATUS			0x0036
#define CHIP_ANA_TEST1			0x0038 //  intended only for debug.
#define CHIP_ANA_TEST2			0x003A //  intended only for debug.
#define CHIP_SHORT_CTRL			0x003C

#define DAP_CONTROL			0x0100
#define DAP_PEQ				0x0102
#define DAP_BASS_ENHANCE		0x0104
#define DAP_BASS_ENHANCE_CTRL		0x0106
#define DAP_AUDIO_EQ			0x0108
#define DAP_SGTL_SURROUND		0x010A
#define DAP_FILTER_COEF_ACCESS		0x010C
#define DAP_COEF_WR_B0_MSB		0x010E
#define DAP_COEF_WR_B0_LSB		0x0110
#define DAP_AUDIO_EQ_BASS_BAND0		0x0116 // 115 Hz
#define DAP_AUDIO_EQ_BAND1		0x0118 // 330 Hz
#define DAP_AUDIO_EQ_BAND2		0x011A // 990 Hz
#define DAP_AUDIO_EQ_BAND3		0x011C // 3000 Hz
#define DAP_AUDIO_EQ_TREBLE_BAND4	0x011E // 9900 Hz
#define DAP_MAIN_CHAN			0x0120
#define DAP_MIX_CHAN			0x0122
#define DAP_AVC_CTRL			0x0124
#define DAP_AVC_THRESHOLD		0x0126
#define DAP_AVC_ATTACK			0x0128
#define DAP_AVC_DECAY			0x012A
#define DAP_COEF_WR_B1_MSB		0x012C
#define DAP_COEF_WR_B1_LSB		0x012E
#define DAP_COEF_WR_B2_MSB		0x0130
#define DAP_COEF_WR_B2_LSB		0x0132
#define DAP_COEF_WR_A1_MSB		0x0134
#define DAP_COEF_WR_A1_LSB		0x0136
#define DAP_COEF_WR_A2_MSB		0x0138
#define DAP_COEF_WR_A2_LSB		0x013A

#define SGTL5000_I2C_ADDR_CS_LOW	0x0A  // CTRL_ADR0_CS pin low (normal configuration)
#define SGTL5000_I2C_ADDR_CS_HIGH	0x2A // CTRL_ADR0_CS  pin high

[[maybe_unused]] static const char kTag[] = "sgtl5000";

// Volume constants — provisional, mirror wm8523 step size for UI compat.
const uint16_t kAbsoluteMaxVolume        = 0x01FF;
const uint16_t kAbsoluteMinVolume        = 0x0000;
const uint16_t kMaxVolumeBeforeClipping  = 0x0190;
const uint16_t kLineLevelReferenceVolume = 0x0180;
const uint16_t kDefaultVolume            = 0x0100;
const uint16_t kDefaultMaxVolume         = kMaxVolumeBeforeClipping;
const uint16_t kZeroDbVolume             = kLineLevelReferenceVolume;

static uint16_t ana_ctrl;
static bool muted;
static bool semi_automated; 
static i2c_master_dev_handle_t sgtl5000_i2c;
static uint8_t i2c_addr = SGTL5000_I2C_ADDR_CS_LOW; 

static bool volumeInteger(unsigned int n); // range: 0x00 to 0x80
static uint8_t calcVol(float n, unsigned char range);
static uint16_t modify(uint16_t reg, uint16_t val, uint16_t iMask);
static uint16_t dap_audio_eq_band(uint8_t bandNum, float n);
static void automate(uint8_t dap, uint8_t eq);
static void automate(uint8_t dap, uint8_t eq, uint8_t filterCount);

static uint16_t sgtl5000_read_register(unsigned int targeted_reg_to_be_read);
static bool sgtl5000_write_register(uint16_t targeted_reg_to_be_written, uint16_t data_to_be_written);
static void sgtl5000_set_address(uint8_t level);
static esp_err_t sgtl5000_init();
static esp_err_t sgtl5000_deinit();

bool drivers::sgtl5000::enable(const unsigned extMCLK, const uint32_t pllFreq) {
    if (sgtl5000_init() != ESP_OK) {
        return false; 
    }
	
	//Check if we are in Master Mode and if the Teensy had a reset:
	uint16_t n = sgtl5000_read_register(CHIP_I2S_CTRL);
	if ( (extMCLK > 0) && (n == (0x0030 | (1<<7))) ) {
		//Yes. Do not initialize.
		muted = false;
		semi_automated = true;
		return true;
	}

	//Serial.print("chip ID = ");
	//delay(5);
	//unsigned int n = sgtl5000_read_register(CHIP_ID);
	//Serial.println(n, HEX);

    muted = true;

	int r = sgtl5000_write_register(CHIP_ANA_POWER, 0x4060);  // VDDD is externally driven with 1.8V
	if (!r) return false;
	sgtl5000_write_register(CHIP_LINREG_CTRL, 0x006C);  // VDDA & VDDIO both over 3.1V
	sgtl5000_write_register(CHIP_REF_CTRL, 0x01F2); // VAG=1.575, normal ramp, +12.5% bias current
	sgtl5000_write_register(CHIP_LINE_OUT_CTRL, 0x0F22); // LO_VAGCNTRL=1.65V, OUT_CURRENT=0.54mA
	sgtl5000_write_register(CHIP_SHORT_CTRL, 0x4446);  // allow up to 125mA
	sgtl5000_write_register(CHIP_ANA_CTRL, 0x0137);  // enable zero cross detectors
		
	if (extMCLK > 0) {
		//SGTL is I2S Master
		//Datasheet Pg. 14: Using the PLL - Asynchronous SYS_MCLK input
		if (extMCLK > 17000000) {
			sgtl5000_write_register(CHIP_CLK_TOP_CTRL, 1);
		} else {
			sgtl5000_write_register(CHIP_CLK_TOP_CTRL, 0);
		}

		uint32_t int_divisor = (pllFreq / extMCLK) & 0x1f;
		uint32_t frac_divisor = (uint32_t)((((float)pllFreq / extMCLK) - int_divisor) * 2048.0f) & 0x7ff;
		
		sgtl5000_write_register(CHIP_PLL_CTRL, (int_divisor << 11) | frac_divisor);		
		sgtl5000_write_register(CHIP_ANA_POWER, 0x40FF | (1<<10) | (1<<8) ); // power up: lineout, hp, adc, dac, PLL_POWERUP, VCOAMP_POWERUP
	} else {
		//SGTL is I2S Slave
		sgtl5000_write_register(CHIP_ANA_POWER, 0x40FF); // power up: lineout, hp, adc, dac
	}

	sgtl5000_write_register(CHIP_DIG_POWER, 0x0073); // power up all digital stuff
	sgtl5000_write_register(CHIP_LINE_OUT_VOL, 0x1D1D); // default approx 1.3 volts peak-to-peak
	
	if (extMCLK > 0) { 
		//SGTL is I2S Master
		sgtl5000_write_register(CHIP_CLK_CTRL, 0x0004 | 0x03);  // 44.1 kHz, 256*Fs, use PLL
		sgtl5000_write_register(CHIP_I2S_CTRL, 0x0030 | (1<<7)); // SCLK=64*Fs, 16bit, I2S format
	} else {
		//SGTL is I2S Slave
		sgtl5000_write_register(CHIP_CLK_CTRL, 0x0004);  // 44.1 kHz, 256*Fs
		sgtl5000_write_register(CHIP_I2S_CTRL, 0x0030); // SCLK=64*Fs, 16bit, I2S format
	}

	// default signal routing is ok?
	sgtl5000_write_register(CHIP_SSS_CTRL, 0x0010); // ADC->I2S, I2S->DAC
	sgtl5000_write_register(CHIP_ADCDAC_CTRL, 0x0000); // disable dac mute
	sgtl5000_write_register(CHIP_DAC_VOL, 0x3C3C); // digital gain, 0dB
	sgtl5000_write_register(CHIP_ANA_HP_CTRL, 0x7F7F); // set volume (lowest level)
	sgtl5000_write_register(CHIP_ANA_CTRL, 0x0036);  // enable zero cross detectors

	semi_automated = true;
    return true;
}

bool drivers::sgtl5000::enable(void) {
#if defined(KINETISL)
	return enable(16000000); // SGTL as Master with 16MHz MCLK from Teensy LC
#else	
	return enable(0);
#endif
}

bool drivers::sgtl5000::volume(float left, float right)
{
	uint16_t m=((0x7F-calcVol(right,0x7F))<<8)|(0x7F-calcVol(left,0x7F));
	return sgtl5000_write_register(CHIP_ANA_HP_CTRL, m);
}

bool drivers::sgtl5000::volume(float n) { return volumeInteger(n * 129 + 0.499f); }

bool drivers::sgtl5000::muteHeadphone(void) { return sgtl5000_write_register(0x0024, ana_ctrl | (1<<4)); }

bool drivers::sgtl5000::unmuteHeadphone(void) { return sgtl5000_write_register(0x0024, ana_ctrl & ~(1<<4)); }

bool drivers::sgtl5000::muteLineout(void) { return sgtl5000_write_register(0x0024, ana_ctrl | (1<<8)); }

bool drivers::sgtl5000::unmuteLineout(void) { return sgtl5000_write_register(0x0024, ana_ctrl & ~(1<<8)); }

bool drivers::sgtl5000::inputSelect(int n) {
    if (n == AUDIO_INPUT_LINEIN) {
        return sgtl5000_write_register(0x0020, 0x055) // +7.5dB gain (1.3Vp-p full scale)
            && sgtl5000_write_register(0x0024, ana_ctrl | (1<<2)); // enable linein
    } else if (n == AUDIO_INPUT_MIC) {
        return sgtl5000_write_register(0x002A, 0x0173) // mic preamp gain = +40dB
            && sgtl5000_write_register(0x0020, 0x088)     // input gain +12dB (is this enough?)
            && sgtl5000_write_register(0x0024, ana_ctrl & ~(1<<2)); // enable mic
    } else {
        return false;
    }
}

bool drivers::sgtl5000::headphoneSelect(int n) {
    if (n == AUDIO_HEADPHONE_DAC) {
        return sgtl5000_write_register(0x0024, ana_ctrl | (1<<6)); // route DAC to headphones out
    } else if (n == AUDIO_HEADPHONE_LINEIN) {
        return sgtl5000_write_register(0x0024, ana_ctrl & ~(1<<6)); // route linein to headphones out
    } else {
        return false;
    }
}

bool drivers::sgtl5000::micGain(unsigned int dB)
{
	unsigned int preamp_gain, input_gain;

	if (dB >= 40) {
		preamp_gain = 3;
		dB -= 40;
	} else if (dB >= 30) {
		preamp_gain = 2;
		dB -= 30;
	} else if (dB >= 20) {
		preamp_gain = 1;
		dB -= 20;
	} else {
		preamp_gain = 0;
	}
	input_gain = (dB * 2) / 3;
	if (input_gain > 15) input_gain = 15;

	return sgtl5000_write_register(CHIP_MIC_CTRL, 0x0170 | preamp_gain)
	    && sgtl5000_write_register(CHIP_ANA_ADC_CTRL, (input_gain << 4) | input_gain);
}

// CHIP_ANA_ADC_CTRL
// Actual measured full-scale peak-to-peak sine wave input for max signal
//  0: 3.12 Volts p-p
//  1: 2.63 Volts p-p
//  2: 2.22 Volts p-p
//  3: 1.87 Volts p-p
//  4: 1.58 Volts p-p
//  5: 1.33 Volts p-p
//  6: 1.11 Volts p-p
//  7: 0.94 Volts p-p
//  8: 0.79 Volts p-p
//  9: 0.67 Volts p-p
// 10: 0.56 Volts p-p
// 11: 0.48 Volts p-p
// 12: 0.40 Volts p-p
// 13: 0.34 Volts p-p
// 14: 0.29 Volts p-p
// 15: 0.24 Volts p-p
bool drivers::sgtl5000::lineInLevel(uint8_t left, uint8_t right)
{
	if (left > 15) left = 15;
	if (right > 15) right = 15;
	return sgtl5000_write_register(CHIP_ANA_ADC_CTRL, (left << 4) | right);
}

bool drivers::sgtl5000::lineInLevel(uint8_t n) { return lineInLevel(n, n); }

// CHIP_LINE_OUT_VOL
//  Actual measured full-scale peak-to-peak sine wave output voltage:
//  0-12: output has clipping
//  13: 3.16 Volts p-p
//  14: 2.98 Volts p-p
//  15: 2.83 Volts p-p
//  16: 2.67 Volts p-p
//  17: 2.53 Volts p-p
//  18: 2.39 Volts p-p
//  19: 2.26 Volts p-p
//  20: 2.14 Volts p-p
//  21: 2.02 Volts p-p
//  22: 1.91 Volts p-p
//  23: 1.80 Volts p-p
//  24: 1.71 Volts p-p
//  25: 1.62 Volts p-p
//  26: 1.53 Volts p-p
//  27: 1.44 Volts p-p
//  28: 1.37 Volts p-p
//  29: 1.29 Volts p-p
//  30: 1.22 Volts p-p
//  31: 1.16 Volts p-p
uint16_t drivers::sgtl5000::lineOutLevel(uint8_t n)
{
	if (n > 31) n = 31;
	else if (n < 13) n = 13;
	return modify(CHIP_LINE_OUT_VOL,(n<<8)|n,(31<<8)|31);
}

uint16_t drivers::sgtl5000::lineOutLevel(uint8_t left, uint8_t right)
{
	if (left > 31) left = 31;
	else if (left < 13) left = 13;
	if (right > 31) right = 31;
	else if (right < 13) right = 13;
	return modify(CHIP_LINE_OUT_VOL,(right<<8)|left,(31<<8)|31);
}

uint16_t drivers::sgtl5000::dacVolume(float n) // set both directly
{
	if ((sgtl5000_read_register(CHIP_ADCDAC_CTRL)&(3<<2)) != ((n>0 ? 0:3)<<2)) {
		modify(CHIP_ADCDAC_CTRL,(n>0 ? 0:3)<<2,3<<2);
	}
	unsigned char m=calcVol(n,0xC0);
	return modify(CHIP_DAC_VOL,((0xFC-m)<<8)|(0xFC-m),65535);
}
uint16_t drivers::sgtl5000::dacVolume(float left, float right)
{
	uint16_t adcdac=((right>0 ? 0:2)|(left>0 ? 0:1))<<2;
	if ((sgtl5000_read_register(CHIP_ADCDAC_CTRL)&(3<<2)) != adcdac) {
		modify(CHIP_ADCDAC_CTRL,adcdac,1<<2);
	}
	uint16_t m=(0xFC-calcVol(right,0xC0))<<8|(0xFC-calcVol(left,0xC0));
	return modify(CHIP_DAC_VOL,m,65535);
}

bool drivers::sgtl5000::dacVolumeRamp()
{
	return modify(CHIP_ADCDAC_CTRL, 0x300, 0x300);
}

bool drivers::sgtl5000::dacVolumeRampLinear()
{
	return modify(CHIP_ADCDAC_CTRL, 0x200, 0x300);
}

bool drivers::sgtl5000::dacVolumeRampDisable()
{
	return modify(CHIP_ADCDAC_CTRL, 0, 0x300);
}

uint16_t drivers::sgtl5000::adcHighPassFilterEnable(void)
{
	return modify(CHIP_ADCDAC_CTRL, 0, 3);
}

uint16_t drivers::sgtl5000::adcHighPassFilterFreeze(void)
{
	return modify(CHIP_ADCDAC_CTRL, 2, 3);
}

uint16_t drivers::sgtl5000::adcHighPassFilterDisable(void)
{
	return modify(CHIP_ADCDAC_CTRL, 1, 3);
}


// DAP_CONTROL

uint16_t drivers::sgtl5000::audioPreProcessorEnable(void)
{
	// audio processor used to pre-process analog input before Teensy
	return sgtl5000_write_register(DAP_CONTROL, 1) && sgtl5000_write_register(CHIP_SSS_CTRL, 0x0013);
}

uint16_t drivers::sgtl5000::audioPostProcessorEnable(void)
{
	// audio processor used to post-process Teensy output before headphones/lineout
	return sgtl5000_write_register(DAP_CONTROL, 1) && sgtl5000_write_register(CHIP_SSS_CTRL, 0x0070);
}

uint16_t drivers::sgtl5000::audioProcessorDisable(void)
{
	return sgtl5000_write_register(CHIP_SSS_CTRL, 0x0010) && sgtl5000_write_register(DAP_CONTROL, 0);
}


// DAP_PEQ
uint16_t drivers::sgtl5000::eqFilterCount(uint8_t n) // valid to n&7, 0 thru 7 filters enabled.
{
	return modify(DAP_PEQ,(n&7),7);
}

// DAP_AUDIO_EQ
uint16_t drivers::sgtl5000::eqSelect(uint8_t n) // 0=NONE, 1=PEQ (7 IIR Biquad filters), 2=TONE (tone), 3=GEQ (5 band EQ)
{
	return modify(DAP_AUDIO_EQ,n&3,3);
}

uint16_t drivers::sgtl5000::eqBand(uint8_t bandNum, float n)
{
	if(semi_automated) automate(1,3);
	return dap_audio_eq_band(bandNum, n);
}
void drivers::sgtl5000::eqBands(float bass, float mid_bass, float midrange, float mid_treble, float treble)
{
	if(semi_automated) automate(1,3);
	dap_audio_eq_band(0,bass);
	dap_audio_eq_band(1,mid_bass);
	dap_audio_eq_band(2,midrange);
	dap_audio_eq_band(3,mid_treble);
	dap_audio_eq_band(4,treble);
}
void drivers::sgtl5000::eqBands(float bass, float treble) // dap_audio_eq(2);
{
	if(semi_automated) automate(1,2);
	dap_audio_eq_band(0,bass);
	dap_audio_eq_band(4,treble);
}

// SGTL5000 PEQ Coefficient loader
void drivers::sgtl5000::eqFilter(uint8_t filterNum, int *filterParameters)
{
	// TODO: add the part that selects 7 PEQ filters.
	if(semi_automated) automate(1,1,filterNum+1);
	modify(DAP_FILTER_COEF_ACCESS,(uint16_t)filterNum,15);
	sgtl5000_write_register(DAP_COEF_WR_B0_MSB,(*filterParameters>>4)&65535);
	sgtl5000_write_register(DAP_COEF_WR_B0_LSB,(*filterParameters++)&15);
	sgtl5000_write_register(DAP_COEF_WR_B1_MSB,(*filterParameters>>4)&65535);
	sgtl5000_write_register(DAP_COEF_WR_B1_LSB,(*filterParameters++)&15);
	sgtl5000_write_register(DAP_COEF_WR_B2_MSB,(*filterParameters>>4)&65535);
	sgtl5000_write_register(DAP_COEF_WR_B2_LSB,(*filterParameters++)&15);
	sgtl5000_write_register(DAP_COEF_WR_A1_MSB,(*filterParameters>>4)&65535);
	sgtl5000_write_register(DAP_COEF_WR_A1_LSB,(*filterParameters++)&15);
	sgtl5000_write_register(DAP_COEF_WR_A2_MSB,(*filterParameters>>4)&65535);
	sgtl5000_write_register(DAP_COEF_WR_A2_LSB,(*filterParameters++)&15);
	sgtl5000_write_register(DAP_FILTER_COEF_ACCESS,(uint16_t)0x100|filterNum);
}

/* Valid values for dap_avc parameters

	maxGain; Maximum gain that can be applied
	0 - 0 dB
	1 - 6.0 dB
	2 - 12 dB

	lbiResponse; Integrator Response
	0 - 0 mS
	1 - 25 mS
	2 - 50 mS
	3 - 100 mS

	hardLimit
	0 - Hard limit disabled. AVC Compressor/Expander enabled.
	1 - Hard limit enabled. The signal is limited to the programmed threshold (signal saturates at the threshold)

	threshold
	floating point in range 0 to -96 dB

	attack
	floating point figure is dB/s rate at which gain is increased

	decay
	floating point figure is dB/s rate at which gain is reduced
*/
uint16_t drivers::sgtl5000::autoVolumeControl(uint8_t maxGain, uint8_t lbiResponse, uint8_t hardLimit, float threshold, float attack, float decay)
{
	//if(semi_automated&&(!sgtl5000_read_register(DAP_CONTROL)&1)) audioProcessorEnable();
	if(maxGain>2) maxGain=2;
	lbiResponse&=3;
	hardLimit&=1;
	uint8_t thresh=(pow(10,threshold/20)*0.636)*pow(2,15);
	uint8_t att=(1-pow(10,-(attack/(20*44100))))*pow(2,19);
	uint8_t dec=(1-pow(10,-(decay/(20*44100))))*pow(2,23);
	sgtl5000_write_register(DAP_AVC_THRESHOLD,thresh);
	sgtl5000_write_register(DAP_AVC_ATTACK,att);
	sgtl5000_write_register(DAP_AVC_DECAY,dec);
	return 	modify(DAP_AVC_CTRL,maxGain<<12|lbiResponse<<8|hardLimit<<5,3<<12|3<<8|1<<5);
}
uint16_t drivers::sgtl5000::autoVolumeEnable(void)
{
	return modify(DAP_AVC_CTRL, 1, 1);
}
uint16_t drivers::sgtl5000::autoVolumeDisable(void)
{
	return modify(DAP_AVC_CTRL, 0, 1);
}

uint16_t drivers::sgtl5000::enhanceBass(float lr_lev, float bass_lev)
{
	return modify(DAP_BASS_ENHANCE_CTRL,((0x3F-calcVol(lr_lev,0x3F))<<8) | (0x7F-calcVol(bass_lev,0x7F)), (0x3F<<8) | 0x7F);
}
uint16_t drivers::sgtl5000::enhanceBass(float lr_lev, float bass_lev, uint8_t hpf_bypass, uint8_t cutoff)
{
	modify(DAP_BASS_ENHANCE,(hpf_bypass&1)<<8|(cutoff&7)<<4,1<<8|7<<4);
	return enhanceBass(lr_lev,bass_lev);
}
uint16_t drivers::sgtl5000::enhanceBassEnable(void)
{
	return modify(DAP_BASS_ENHANCE, 1, 1);
}
uint16_t drivers::sgtl5000::enhanceBassDisable(void)
{
	return modify(DAP_BASS_ENHANCE, 0, 1);
}
uint16_t drivers::sgtl5000::surroundSound(uint8_t width)
{
	return modify(DAP_SGTL_SURROUND,(width&7)<<4,7<<4);
}
uint16_t drivers::sgtl5000::surroundSound(uint8_t width, uint8_t select)
{
	return modify(DAP_SGTL_SURROUND,((width&7)<<4)|(select&3), (7<<4)|3);
}
uint16_t drivers::sgtl5000::surroundSoundEnable(void)
{
	return modify(DAP_SGTL_SURROUND, 3, 3);
}
uint16_t drivers::sgtl5000::surroundSoundDisable(void)
{
	return modify(DAP_SGTL_SURROUND, 0, 3);
}

void drivers::sgtl5000::killAutomation(void) { semi_automated=false; }

static uint16_t modify(uint16_t reg,  uint16_t val, uint16_t iMask)
{
	uint16_t val1 = (sgtl5000_read_register(reg)&(~iMask))|val;
	if(!sgtl5000_write_register(reg,val1)) return 0;
	return val1;
}

static bool volumeInteger(unsigned int n)
{
	if (n == 0) {
		muted = true;
		sgtl5000_write_register(CHIP_ANA_HP_CTRL, 0x7F7F);
		return drivers::sgtl5000::muteHeadphone();
	} else if (n > 0x80) {
		n = 0;
	} else {
		n = 0x80 - n;
	}
	if (muted) {
		muted = false;
		drivers::sgtl5000::unmuteHeadphone();
	}
	n = n | (n << 8);
	return sgtl5000_write_register(CHIP_ANA_HP_CTRL, n);  // set volume
}

static uint8_t calcVol(float n, unsigned char range)
{
	// n=(n*(((float)range)/100))+0.499;
	n=(n*(float)range)+(float)0.499;
	if ((unsigned char)n>range) n=range;
	return (unsigned char)n;
}

// DAP_AUDIO_EQ_BASS_BAND0 & DAP_AUDIO_EQ_BAND1 & DAP_AUDIO_EQ_BAND2 etc etc
static uint16_t dap_audio_eq_band(uint8_t bandNum, float n) // by signed percentage -100/+100; dap_audio_eq(3);
{
	n=(n*48)+(float)0.499;
	if(n<-47) n=-47;
	if(n>48) n=48;
	n+=47;
	return modify(DAP_AUDIO_EQ_BASS_BAND0+(bandNum*2),(unsigned int)n,127);
}

static void automate(uint8_t dap, uint8_t eq)
{
	//if((dap!=0)&&(!(sgtl5000_read_register(DAP_CONTROL)&1))) audioProcessorEnable();
	if((sgtl5000_read_register(DAP_AUDIO_EQ)&3) != eq) drivers::sgtl5000::eqSelect(eq);
}

static void automate(uint8_t dap, uint8_t eq, uint8_t filterCount)
{
	automate(dap,eq);
	if (filterCount > (sgtl5000_read_register(DAP_PEQ)&7)) drivers::sgtl5000::eqFilterCount(filterCount);
}


// if(SGTL5000_PEQ) quantization_unit=524288; if(AudioFilterBiquad) quantization_unit=2147483648;
void calcBiquad(uint8_t filtertype, float fC, float dB_Gain, float Q, uint32_t quantization_unit, uint32_t fS, int *coef)
{

// I used resources like http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt
// to make this routine, I tested most of the filter types and they worked. Such filters have limits and
// before calling this routine with varying values the end user should check that those values are limited
// to valid results.

  float A;
  if(filtertype<FILTER_PARAEQ) A=pow(10,dB_Gain/20); else A=pow(10,dB_Gain/40);
  float W0 = 2*(float)3.14159265358979323846*fC/fS;
  float cosw=cosf(W0);
  float sinw=sinf(W0);
  //float alpha = sinw*sinh((log(2)/2)*BW*W0/sinw);
  //float beta = sqrt(2*A);
  float alpha = sinw / (2 * Q);
  float beta = sqrtf(A)/Q;
  float b0,b1,b2,a0,a1,a2;

  switch(filtertype) {
  case FILTER_LOPASS:
    b0 = (1.0F - cosw) * 0.5F; // =(1-COS($H$2))/2
    b1 = 1.0F - cosw;
    b2 = (1.0F - cosw) * 0.5F;
    a0 = 1.0F + alpha;
    a1 = 2.0F * cosw;
    a2 = alpha - 1.0F;
  break;
  case FILTER_HIPASS:
    b0 = (1.0F + cosw) * 0.5F;
    b1 = -(cosw + 1.0F);
    b2 = (1.0F + cosw) * 0.5F;
    a0 = 1.0F + alpha;
    a1 = 2.0F * cosw;
    a2 = alpha - 1.0F;
  break;
  case FILTER_BANDPASS:
    b0 = alpha;
    b1 = 0.0F;
    b2 = -alpha;
    a0 = 1.0F + alpha;
    a1 = 2.0F * cosw;
    a2 = alpha - 1.0F;
   break;
  case FILTER_NOTCH:
    b0=1;
    b1=-2*cosw;
    b2=1;
    a0=1+alpha;
    a1=2*cosw;
    a2=-(1-alpha);
  break;
  case FILTER_PARAEQ:
    b0 = 1 + (alpha*A);
    b1 =-2 * cosw;
    b2 = 1 - (alpha*A);
    a0 = 1 + (alpha/A);
    a1 = 2 * cosw;
    a2 =-(1-(alpha/A));
  break;
  case FILTER_LOSHELF:
    b0 = A * ((A+1.0F) - ((A-1.0F)*cosw) + (beta*sinw));
    b1 = 2.0F * A * ((A-1.0F) - ((A+1.0F)*cosw));
    b2 = A * ((A+1.0F) - ((A-1.0F)*cosw) - (beta*sinw));
    a0 = (A+1.0F) + ((A-1.0F)*cosw) + (beta*sinw);
    a1 = 2.0F * ((A-1.0F) + ((A+1.0F)*cosw));
    a2 = -((A+1.0F) + ((A-1.0F)*cosw) - (beta*sinw));
  break;
  case FILTER_HISHELF:
    b0 = A * ((A+1.0F) + ((A-1.0F)*cosw) + (beta*sinw));
    b1 = -2.0F * A * ((A-1.0F) + ((A+1.0F)*cosw));
    b2 = A * ((A+1.0F) + ((A-1.0F)*cosw) - (beta*sinw));
    a0 = (A+1.0F) - ((A-1.0F)*cosw) + (beta*sinw);
    a1 = -2.0F * ((A-1.0F) - ((A+1.0F)*cosw));
    a2 = -((A+1.0F) - ((A-1.0F)*cosw) - (beta*sinw));
  break;
  default:
    b0 = 0.5;
    b1 = 0.0;
    b2 = 0.0;
    a0 = 1.0;
    a1 = 0.0;
    a2 = 0.0;
  }

  a0=(a0*2)/(float)quantization_unit; // once here instead of five times there...
  b0/=a0;
  *coef++=(int)((float)b0+(float)0.499);
  b1/=a0;
  *coef++=(int)((float)b1+(float)0.499);
  b2/=a0;
  *coef++=(int)((float)b2+(float)0.499);
  a1/=a0;
  *coef++=(int)((float)a1+(float)0.499);
  a2/=a0;
  *coef++=(int)((float)a2+(float)0.499);
}

static uint16_t sgtl5000_read_register(unsigned int targeted_reg_to_be_read) {
    uint8_t cmd[] = {static_cast<uint8_t>(targeted_reg_to_be_read >> 8), static_cast<uint8_t>(targeted_reg_to_be_read)};
    uint8_t data[] = {0, 0};
    if (i2c_master_transmit_receive(sgtl5000_i2c, cmd, 1, data, 2, 100) != ESP_OK) {
        return {};
    }
    return (data[0] << 8) | data[1];
}

static bool sgtl5000_write_register(uint16_t targeted_reg_to_be_written, uint16_t data_to_be_written) {
    if (targeted_reg_to_be_written == CHIP_ANA_CTRL) ana_ctrl = data_to_be_written;
    uint8_t buffer_reg[] = {static_cast<uint8_t>(targeted_reg_to_be_written >> 8) , static_cast<uint8_t>(targeted_reg_to_be_written)};
    uint8_t buffer_data[] = {static_cast<uint8_t>(data_to_be_written >> 8), static_cast<uint8_t>(data_to_be_written)}; 

    if (i2c_master_transmit(sgtl5000_i2c, buffer_reg, sizeof(buffer_data), 100) != ESP_OK
        && i2c_master_transmit(sgtl5000_i2c, buffer_data, sizeof(buffer_data), 100) != ESP_OK) {
        return false;
    }
    
    return true;
}

static void sgtl5000_set_address(uint8_t level) {
    if(level == LOW) {
        i2c_addr = SGTL5000_I2C_ADDR_CS_LOW;
    } else {
        i2c_addr = SGTL5000_I2C_ADDR_CS_HIGH;
    }
}

static esp_err_t sgtl5000_init() {
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 400'000,
        .scl_wait_us = 0,
        .flags = {.disable_ack_check = false},
    };
    return i2c_master_bus_add_device(drivers::i2c_handle(), &config, &sgtl5000_i2c);  
}

static esp_err_t sgtl5000_deinit() {
    return i2c_master_bus_rm_device(sgtl5000_i2c);
}

