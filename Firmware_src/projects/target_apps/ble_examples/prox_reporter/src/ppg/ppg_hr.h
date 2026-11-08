#ifndef _PPG_HR_H_
#define _PPG_HR_H_

#include <stdint.h>

// Heart rate from the PPG LED channel, on the ring: drift removal (centred 1.5 s
// moving average), 3-sample smoothing, autocorrelation over the last 8 s (40-180
// bpm) with parabolic interpolation, period -> bpm with the measured sample rate.
// Same method as the web app (apps/ppg), validated on recordings from worn rings.

// Reported heart rate: only with a clear pulse (confidence >= PPG_HR_CONF_MIN); a
// brief dip keeps the last one for up to PPG_HR_HOLD updates (~1 s apart)
#define PPG_HR_CONF_MIN 50
#define PPG_HR_HOLD     5

// Forget the samples and the reported heart rate (after an LED or gain change: the
// signal jumps; while the hand moves)
void ppg_hr_reset(void);

// One LED channel sample
void ppg_hr_add(int32_t sample);

// fs_x100: sample rate in 1/100 Hz. Outputs: bpm_x10 (0: not enough data) and
// confidence 0-100 (autocorrelation at the period: > 50 is a clear pulse)
void ppg_hr_compute(uint16_t fs_x100, uint16_t *bpm_x10, uint8_t *confidence);

// ppg_hr_compute() + the reporting rules above: bpm_x10 = 0 without a clear pulse
// (confidence is still the current one)
void ppg_hr_update(uint16_t fs_x100, uint16_t *bpm_x10, uint8_t *confidence);

#endif // _PPG_HR_H_
