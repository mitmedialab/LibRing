#include "ppg_spo2.h"

#include <math.h>

#define BUF       256  // > 10 s at 24.2 Hz
#define WINDOW_S  8
#define MIN_S     5

static int32_t red_buf[BUF], ir_buf[BUF];  // circular, same frames
static uint16_t head, count;
static float red_ac[BUF], ir_ac[BUF];      // pulse signals (static: the stack is small)
static uint16_t last[PPG_SPO2_AVERAGE];    // last valid estimates (x10)
static uint8_t n_last;

void ppg_spo2_reset(void)
{
    head = 0;
    count = 0;
    n_last = 0;
}

void ppg_spo2_add(int32_t red, int32_t ir)
{
    red_buf[head] = red;
    ir_buf[head] = ir;
    head = (head + 1) % BUF;
    if (count < BUF) {
        count++;
    }
}

static int32_t window_mean(const int32_t *buf, int n)
{
    int64_t sum = 0;
    for (int i = 0; i < n; i++) {
        sum += buf[(head + BUF - n + i) % BUF];
    }
    return n ? (int32_t)(sum / n) : 0;
}

void ppg_spo2_means(int32_t *red, int32_t *ir)
{
    int n = count < 24 ? count : 24; // last ~1 s
    *red = window_mean(red_buf, n);
    *ir = window_mean(ir_buf, n);
}

// Pulse of the last n samples of buf into out (m = n - 2 half - 2 values): x minus
// its centred moving average (2 half + 1), then 3-sample smoothing. Returns its RMS.
static float pulse(const int32_t *buf, int n, int half, float *out)
{
    int m = n - 2 * half;
    int64_t sum = 0;
    for (int i = 0; i <= 2 * half; i++) {
        sum += buf[(head + BUF - n + i) % BUF];
    }
    for (int i = 0; i < m; i++) {
        out[i] = buf[(head + BUF - n + i + half) % BUF] - (float)sum / (2 * half + 1);
        if (i + 2 * half + 1 < n) {
            sum += buf[(head + BUF - n + i + 2 * half + 1) % BUF] - buf[(head + BUF - n + i) % BUF];
        }
    }
    float mean = 0;
    for (int i = 0; i < m - 2; i++) {
        out[i] = (out[i] + out[i + 1] + out[i + 2]) / 3;
        mean += out[i];
    }
    m -= 2;
    mean /= m;
    float energy = 0;
    for (int i = 0; i < m; i++) {
        out[i] -= mean;
        energy += out[i] * out[i];
    }
    return sqrtf(energy / m);
}

void ppg_spo2_compute(uint16_t fs_x100, int32_t red_offset, int32_t ir_offset,
                      uint16_t *ratio_x1000, uint8_t *corr, uint16_t *spo2_x10)
{
    *ratio_x1000 = 0;
    *corr = 0;
    *spo2_x10 = 0;
    int n = (int)((uint32_t)WINDOW_S * fs_x100 / 100);
    int half = (int)((uint32_t)75 * fs_x100 / 10000); // 0.75 s
    if (n > count) {
        n = count;
    }
    if (fs_x100 < 500 || n < (int)((uint32_t)MIN_S * fs_x100 / 100) || n <= 2 * half + 8) {
        n_last = 0;
        return;
    }
    float red_rms = pulse(red_buf, n, half, red_ac);
    float ir_rms = pulse(ir_buf, n, half, ir_ac);
    int m = n - 2 * half - 2;
    float cross = 0;
    for (int i = 0; i < m; i++) {
        cross += red_ac[i] * ir_ac[i];
    }
    float c = red_rms > 0 && ir_rms > 0 ? cross / m / (red_rms * ir_rms) : 0;
    *corr = c > 0 ? (uint8_t)(c * 100 + 0.5f) : 0;

    float red_dc = (float)window_mean(red_buf, n) + red_offset;
    float ir_dc = (float)window_mean(ir_buf, n) + ir_offset;
    if (red_dc <= 0 || ir_dc <= 0 || ir_rms <= 0) {
        return;
    }
    float r = (red_rms / red_dc) / (ir_rms / ir_dc);
    *ratio_x1000 = r < 65.0f ? (uint16_t)(r * 1000 + 0.5f) : 65000;
    if (*corr < PPG_SPO2_CORR_MIN) {
        n_last = 0;
        return;
    }
    float spo2 = 110.0f - 25.0f * r;
    if (spo2 > 100.0f) {
        spo2 = 100.0f;
    } else if (spo2 < 50.0f) {
        n_last = 0;
        return;
    }
    // Mean of the last valid estimates (one per second, overlapping windows)
    if (n_last == PPG_SPO2_AVERAGE) {
        for (int i = 1; i < PPG_SPO2_AVERAGE; i++) {
            last[i - 1] = last[i];
        }
        n_last--;
    }
    last[n_last++] = (uint16_t)(spo2 * 10 + 0.5f);
    uint32_t sum = 0;
    for (int i = 0; i < n_last; i++) {
        sum += last[i];
    }
    *spo2_x10 = (uint16_t)((sum + n_last / 2) / n_last);
}
