#ifndef _PPG_SPO2_H_
#define _PPG_SPO2_H_

#include <stdint.h>

// Blood oxygen (SpO2) from the red and IR channels, on the ring: ratio of ratios
// R = (AC_red / DC_red) / (AC_ir / DC_ir) over the last 8 s, AC = RMS of the pulse
// (drift removed with a centred 1.5 s moving average, 3-sample smoothing, as the
// heart rate), DC = the light that went through the finger (see ppg_spo2_compute()).
// SpO2 = 110 - 25 R (textbook curve for red/IR pulse oximetry, not calibrated for
// this ring). Reported only when the red and IR pulses match (correlation >=
// PPG_SPO2_CORR_MIN): otherwise the ratio is noise.
#define PPG_SPO2_CORR_MIN 90  // x100 (on 2 rings: 0.95-0.97 with a good signal; 0.8 gave 76 % on a noisy one)
#define PPG_SPO2_AVERAGE  5   // reported value: mean of the last valid estimates

// Forget the samples (after a gain change: the signals jump; while the hand moves)
void ppg_spo2_reset(void);

// One frame: raw red and IR readings
void ppg_spo2_add(int32_t red, int32_t ir);

// Mean raw readings of the window (for the leak calibration), 0 without data
void ppg_spo2_means(int32_t *red, int32_t *ir);

// fs_x100: sample rate in 1/100 Hz. red_offset / ir_offset: added to the mean raw
// reading to get the light that went through the finger (offset DAC x counts per
// step - dark reading - optical leak). Outputs: ratio R x1000 (0: not enough data),
// pulse correlation x100, SpO2 x10 (0: no reliable value)
void ppg_spo2_compute(uint16_t fs_x100, int32_t red_offset, int32_t ir_offset,
                      uint16_t *ratio_x1000, uint8_t *corr, uint16_t *spo2_x10);

#endif // _PPG_SPO2_H_
