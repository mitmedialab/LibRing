#include "ppg_hr.h"

#include <stdbool.h>
#include <string.h>

#define HR_BUF        256  // > 10 s at 24.2 Hz
#define HR_WINDOW_S   8
#define HR_MIN_S      5
#define HR_MIN_BPM    40
#define HR_MAX_BPM    180

static int32_t buf[HR_BUF];   // circular
static uint16_t head, count;
static int32_t work[HR_BUF];  // window, then pulse signal
static int32_t ac[HR_BUF];    // drift removed (static: the stack is small)
static uint16_t last_bpm_x10; // last estimate with a clear pulse
static uint8_t last_age;      // ppg_hr_update() calls since then

void ppg_hr_reset(void)
{
    head = 0;
    count = 0;
    last_bpm_x10 = 0;
}

void ppg_hr_add(int32_t sample)
{
    buf[head] = sample;
    head = (head + 1) % HR_BUF;
    if (count < HR_BUF) {
        count++;
    }
}

void ppg_hr_compute(uint16_t fs_x100, uint16_t *bpm_x10, uint8_t *confidence)
{
    *bpm_x10 = 0;
    *confidence = 0;
    if (fs_x100 < 500) {
        return;
    }
    int n = (int)((uint32_t)HR_WINDOW_S * fs_x100 / 100);
    int half = (int)((uint32_t)75 * fs_x100 / 10000); // 0.75 s: centred 1.5 s average
    if (n > count) {
        n = count;
    }
    if (n < (int)((uint32_t)HR_MIN_S * fs_x100 / 100) || n <= 2 * half + 8) {
        return;
    }

    // Last n samples, oldest first
    for (int i = 0; i < n; i++) {
        work[i] = buf[(head + HR_BUF - n + i) % HR_BUF];
    }

    // Drift removal: x[i] - mean(x[i-half .. i+half]), for i in [half, n - half)
    int m = n - 2 * half;
    int64_t sum = 0;
    for (int i = 0; i <= 2 * half; i++) {
        sum += work[i];
    }
    for (int i = 0; i < m; i++) {
        ac[i] = work[i + half] - (int32_t)(sum / (2 * half + 1));
        if (i + 2 * half + 1 < n) {
            sum += work[i + 2 * half + 1] - work[i];
        }
    }
    // 3-sample smoothing, zero mean, scaled to +-1023 (products fit in 32 bits)
    int32_t maxabs = 1;
    int64_t mean = 0;
    for (int i = 1; i < m - 1; i++) {
        work[i - 1] = (ac[i - 1] + ac[i] + ac[i + 1]) / 3;
        mean += work[i - 1];
    }
    m -= 2;
    mean /= m;
    for (int i = 0; i < m; i++) {
        work[i] -= (int32_t)mean;
        int32_t a = work[i] < 0 ? -work[i] : work[i];
        if (a > maxabs) {
            maxabs = a;
        }
    }
    for (int i = 0; i < m; i++) {
        work[i] = (int32_t)((int64_t)work[i] * 1023 / maxabs);
    }

    // Autocorrelation over the lags of HR_MAX_BPM .. HR_MIN_BPM
    int lag_min = (int)((uint32_t)60 * fs_x100 / (100 * HR_MAX_BPM));
    int lag_max = (int)((uint32_t)60 * fs_x100 / (100 * HR_MIN_BPM));
    if (lag_min < 2) {
        lag_min = 2;
    }
    if (lag_max > m / 2) {
        lag_max = m / 2;
    }
    int32_t energy = 0;
    for (int i = 0; i < m; i++) {
        energy += work[i] * work[i];
    }
    if (energy <= 0 || lag_max <= lag_min + 1) {
        return;
    }
    float r_prev = 0, r_best = 0, r_before = 0, r_after = 0;
    int best = 0;
    bool after_pending = false;
    for (int lag = lag_min - 1; lag <= lag_max + 1; lag++) {
        int32_t s = 0;
        for (int i = 0; i + lag < m; i++) {
            s += work[i] * work[i + lag];
        }
        float r = (float)s / energy * m / (m - lag);
        if (after_pending) {
            r_after = r;
            after_pending = false;
        }
        if (lag >= lag_min && lag <= lag_max && r > r_best) {
            r_best = r;
            best = lag;
            r_before = r_prev;
            after_pending = true;
        }
        r_prev = r;
    }
    if (!best || r_best <= 0) {
        return;
    }
    // Parabolic interpolation of the peak
    float denom = r_before - 2 * r_best + r_after;
    float delta = denom < 0 ? 0.5f * (r_before - r_after) / denom : 0;
    if (delta > 0.5f || delta < -0.5f) {
        delta = 0;
    }
    float bpm = 60.0f * fs_x100 / 100.0f / (best + delta);
    *bpm_x10 = (uint16_t)(bpm * 10 + 0.5f);
    *confidence = r_best >= 1 ? 100 : (uint8_t)(r_best * 100);
}

void ppg_hr_update(uint16_t fs_x100, uint16_t *bpm_x10, uint8_t *confidence)
{
    uint16_t bpm;
    ppg_hr_compute(fs_x100, &bpm, confidence);
    if (bpm && *confidence >= PPG_HR_CONF_MIN) {
        last_bpm_x10 = bpm;
        last_age = 0;
    } else if (last_bpm_x10 && ++last_age > PPG_HR_HOLD) {
        last_bpm_x10 = 0;
    }
    *bpm_x10 = last_bpm_x10;
}
