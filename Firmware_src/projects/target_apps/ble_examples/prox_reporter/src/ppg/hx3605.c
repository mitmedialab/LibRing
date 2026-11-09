#include "hx3605.h"

#include <string.h>

#include "arch.h"
#include "i2c.h"
#include "accelerometer.h"
#include "lld_evt.h"
#include "ppg_hr.h"
#include "ppg_spo2.h"

// The target address can only be changed while the I2C controller is disabled
// (same sequence as set_dev_addr() in the SDK examples)
static void i2c_set_target(uint16_t addr)
{
    if (i2c_get_target_address() == addr) {
        return;
    }
    i2c_set_controller_status(I2C_CONTROLLER_DISABLE);
    while (i2c_get_controller_status() != I2C_CONTROLLER_DISABLE);
    i2c_set_target_address(addr);
    i2c_set_controller_status(I2C_CONTROLLER_ENABLE);
    while (i2c_get_controller_status() != I2C_CONTROLLER_ENABLE);
}

static bool read_regs_at(uint16_t addr, uint8_t reg, uint8_t *buf, uint8_t len)
{
    i2c_abort_t abort_code;
    uint16_t previous = i2c_get_target_address();
    i2c_set_target(addr);
    i2c_master_transmit_buffer_sync(&reg, 1, &abort_code, I2C_F_NONE);
    if (abort_code == I2C_ABORT_NONE) {
        i2c_master_receive_buffer_sync(buf, len, &abort_code, I2C_F_WAIT_FOR_STOP);
    }
    i2c_set_target(previous);
    return abort_code == I2C_ABORT_NONE;
}

bool hx3605_read_regs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    return read_regs_at(HX3605_I2C_ADDR, reg, buf, len);
}

bool hx3605_write_reg(uint8_t reg, uint8_t value)
{
    i2c_abort_t abort_code;
    uint8_t data[2] = {reg, value};
    uint16_t previous = i2c_get_target_address();
    i2c_set_target(HX3605_I2C_ADDR);
    i2c_master_transmit_buffer_sync(data, 2, &abort_code, I2C_F_WAIT_FOR_STOP);
    i2c_set_target(previous);
    return abort_code == I2C_ABORT_NONE;
}

// ---- Streaming ----

// true after a FIFO reset: the reference driver drops the first sample then
static bool fifo_first_read = true;

// Sample rate (measured) and heart rate (ppg_hr.c)
#define FS_DEFAULT_X100 2420       // HX3918 at "25 Hz": its clock runs ~3 % slow
#define FS_MIN_SLOTS    8000       // 5 s (625 us BLE slots) before using the measure
#define FS_MAX_SLOTS    96000      // 60 s: restart the measurement window
static uint16_t fs_x100 = FS_DEFAULT_X100;
static uint32_t fs_t0, fs_count;
static uint16_t hr_bpm_x10;
static uint8_t hr_confidence, hr_polls, motion_level;

static void delay_ms(uint32_t ms)
{
    arch_asm_delay_us(ms * 1000);
}

// Heart-rate setup of the stock firmware (Stock_V160.bin, mode 1: 25 Hz, FIFO),
// recorded by emulating its setup function (0x07FC8270) with the chip answering
// ID 0x27: a full register image, the HR configuration, then per-channel values.
// Register 0xff is written as the others (the stock table writer does).
static const uint8_t stock_hr_setup[][2] = {
// 248 register writes, in order
    {0x16, 0x00}, {0x17, 0x5f}, {0x18, 0x00}, {0x19, 0x05}, {0x1a, 0x00}, {0x1b, 0x03},
    {0x1c, 0x20}, {0x1d, 0x20}, {0x1e, 0x20}, {0x1f, 0x20}, {0x20, 0x00}, {0x21, 0x00},
    {0x22, 0x02}, {0x23, 0xff}, {0x24, 0x88}, {0x25, 0x88}, {0x26, 0x00}, {0x27, 0x00},
    {0x28, 0x00}, {0x29, 0x00}, {0x2a, 0x00}, {0x2b, 0x00}, {0x2c, 0x00}, {0x2d, 0x00},
    {0x2e, 0x00}, {0x2f, 0x00}, {0x30, 0x00}, {0x31, 0x08}, {0x32, 0x00}, {0x33, 0x00},
    {0x34, 0x00}, {0x35, 0x01}, {0x36, 0x0f}, {0x37, 0x00}, {0x38, 0x00}, {0x39, 0xff},
    {0x3a, 0x7f}, {0x3b, 0x1c}, {0x3c, 0x00}, {0x3d, 0x00}, {0x3e, 0x20}, {0x3f, 0x00},
    {0x40, 0x00}, {0x41, 0xff}, {0x42, 0xfb}, {0x43, 0xff}, {0x49, 0x14}, {0x4a, 0x05},
    {0x4b, 0x00}, {0x4c, 0x00}, {0x4d, 0x20}, {0x4e, 0x00}, {0x4f, 0x00}, {0x51, 0x00},
    {0x52, 0x02}, {0x53, 0x00}, {0x54, 0x01}, {0x55, 0x03}, {0x56, 0x00}, {0x57, 0x0c},
    {0x58, 0x00}, {0x59, 0x30}, {0x5a, 0x00}, {0x5b, 0x00}, {0x5c, 0x00}, {0x5d, 0x31},
    {0x5e, 0x44}, {0x5f, 0x11}, {0x60, 0x00}, {0x61, 0x00}, {0x62, 0x00}, {0x63, 0x00},
    {0x64, 0x00}, {0x65, 0x00}, {0x66, 0x32}, {0x67, 0x00}, {0x68, 0x32}, {0x69, 0x00},
    {0x6a, 0x00}, {0x6b, 0x00}, {0x6c, 0x05}, {0x6d, 0x00}, {0x6e, 0x19}, {0x6f, 0x01},
    {0x70, 0x88}, {0x71, 0x08}, {0x72, 0x10}, {0x73, 0x11}, {0x74, 0x00}, {0x75, 0x00},
    {0x76, 0x77}, {0x77, 0x77}, {0x78, 0x77}, {0x79, 0x00}, {0x7a, 0x07}, {0x7b, 0x09},
    {0x7c, 0x05}, {0x7d, 0x00}, {0x7e, 0x00}, {0x7f, 0x00}, {0x81, 0x06}, {0x84, 0x00},
    {0x85, 0x01}, {0x86, 0x01}, {0x87, 0x72}, {0x8c, 0x00}, {0x8d, 0x00}, {0x8e, 0x00},
    {0x8f, 0x00}, {0x90, 0x70}, {0x91, 0x00}, {0x93, 0x03}, {0x94, 0x01}, {0x95, 0x00},
    {0x96, 0x00}, {0x97, 0x00}, {0x98, 0x00}, {0x99, 0x40}, {0x9a, 0x40}, {0x9b, 0x40},
    {0x9c, 0x07}, {0x9d, 0x40}, {0x9e, 0x40}, {0x9f, 0x40}, {0xa0, 0x00}, {0xa1, 0x00},
    {0xa2, 0x00}, {0xa3, 0x00}, {0xa4, 0x00}, {0xa5, 0x20}, {0xa6, 0x20}, {0xa7, 0x20},
    {0xa8, 0x00}, {0xa9, 0x00}, {0xaa, 0x00}, {0xab, 0x00}, {0xac, 0x00}, {0xad, 0x00},
    {0xae, 0x00}, {0xaf, 0xc9}, {0xb0, 0x52}, {0xbf, 0x45}, {0xc0, 0x3d}, {0xc1, 0x04},
    {0xc2, 0xff}, {0xc3, 0xff}, {0xc4, 0x07}, {0xc8, 0x08}, {0xc9, 0x2a}, {0xca, 0x05},
    {0xcb, 0xae}, {0xcc, 0x27}, {0xcd, 0x05}, {0xf0, 0x22}, {0xf1, 0x28}, {0xf2, 0x04},
    {0xf3, 0x70}, {0xf4, 0x12}, {0xf5, 0x89}, {0xf6, 0x2a}, {0xf7, 0x85}, {0xf8, 0x37},
    {0xf9, 0x00}, {0xf8, 0x88}, {0xfb, 0x40}, {0xff, 0x0a}, {0x4d, 0x18}, {0x4e, 0x40},
    {0x4b, 0x00}, {0x4b, 0x04}, {0xff, 0x0a}, {0x16, 0x0f}, {0x17, 0xf5}, {0x18, 0x00},
    {0x19, 0x05}, {0x1a, 0x00}, {0x1b, 0x5e}, {0x1c, 0x10}, {0x1d, 0x10}, {0x1e, 0x20},
    {0x1f, 0x20}, {0x20, 0x00}, {0x21, 0x00}, {0x23, 0xaf}, {0x24, 0x55}, {0x25, 0x55},
    {0x26, 0x0a}, {0x27, 0x00}, {0x28, 0x00}, {0x29, 0x00}, {0x2a, 0x20}, {0x2b, 0x10},
    {0x2c, 0x00}, {0x2d, 0x00}, {0x2e, 0x00}, {0x2f, 0x00}, {0x36, 0x18}, {0x4e, 0x00},
    {0x6d, 0x00}, {0xf0, 0x22}, {0xf1, 0x28}, {0xf2, 0x04}, {0xf3, 0x70}, {0xf4, 0x32},
    {0xf5, 0x89}, {0xf6, 0x2b}, {0xf7, 0x85}, {0xf8, 0x27}, {0xf9, 0x00}, {0xfa, 0x88},
    {0xfb, 0x40}, {0xf6, 0x2b}, {0x3d, 0x83}, {0x3e, 0x20}, {0x3c, 0x1f}, {0x37, 0x00},
    {0x3b, 0x00}, {0x3b, 0x10}, {0x4d, 0x01}, {0xff, 0x05}, {0x4d, 0x00}, {0xff, 0x05},
    {0x6a, 0x00}, {0x55, 0x30}, {0x57, 0x2c}, {0x59, 0x0c}, {0x5b, 0x11}, {0x5d, 0x25},
    {0x5e, 0x22}, {0x5f, 0x11}, {0x66, 0x08}, {0x67, 0x00}, {0x68, 0x08}, {0x69, 0x00},
    {0x6e, 0x11}, {0x77, 0x70}, {0x90, 0x02}, {0x8f, 0x00}, {0x95, 0x00}, {0x9e, 0x41},
    {0xa1, 0x00}, {0xa6, 0x2e}, {0xa9, 0x00}, {0x9d, 0x29}, {0xa0, 0x00}, {0xa5, 0x1f},
    {0xa8, 0x00}, {0x6a, 0x02},
};

// Stock "off" table (0x07FD03E0): ends with 0x01 = 0x01 (sleep)
static const uint8_t stock_off[][2] = {
    {0x2a, 0x00}, {0x2b, 0x00}, {0x16, 0x00}, {0x01, 0x01},
};

// SpO2 mode, applied over the heart-rate setup: the vendor's phase layout
// (hx3918_spo2_ppg_init(): an LED on phase 0 gets jumps in its readings) with green
// kept for the heart rate: phase 0 dark, 1 red, 2 green, 3 IR. Red/IR currents and
// offset DACs: spo2_channel_step(); phase 2/3 LED select (0x2b): ppg_set_led().
static uint8_t mode = PPG_MODE_HR;
static const uint8_t spo2_setup[][2] = {
    {0x17, 0xff},                             // full resolution (OSR 1024) on all phases: the
                                              // HR setup has 256 on phases 0/1 (saturates at ~32k)
    {0x1c, 0x40}, {0x1d, 0x40}, {0x1e, 0x40}, {0x1f, 0x40}, // 128 us pulses
    {0x2a, 0x04},                             // phase 0: no LED, phase 1: red (LDR2)
    {0x23, 0x7f},                             // LEDs on phases 1-3, offset DACs on
    {0x26, 0x00}, {0x2c, 0x00},
    {0x3d, 0x00}, {0x3d, 0x80},               // FIFO reset, then the 4 raw phases (flag = phase)
};
#define LDR_IR_PHASE3 0x02  // 0x2b bits 3:0 in SpO2 mode

// Red and IR channels. The LEDs are so close to the photodiode that they saturate
// it even at the lowest current: the offset DAC of their phase (current subtracted
// from the photodiode) keeps the raw reading in 150k-350k, ~140k counts per step.
// A low current is enough (the signal-to-noise ratio doesn't improve with it).
#define SPO2_RED_CURRENT 8
#define SPO2_IR_CURRENT  24
#define RAW_CLIP_LOW     20000   // the raw readings clip at ~1k and ~523k
#define RAW_CLIP_HIGH    500000
#define RAW_LOW          150000
#define RAW_HIGH         350000
#define RAW_TARGET       250000
#define RAW_RECAL_LOW    100000
#define RAW_RECAL_HIGH   400000
static int32_t idac_counts = 140000; // raw counts per offset DAC step, measured on the go
static struct spo2_ch {
    uint8_t phase, cur_reg, idac_reg, current, idac, out_count;
    bool locked;
    int8_t step;    // last offset DAC change (for idac_counts)
    int32_t before; // raw reading before it (0: none)
} spo2_ch[2] = {
    {1, 0x27, 0x2d, SPO2_RED_CURRENT},
    {3, 0x29, 0x2f, SPO2_IR_CURRENT},
};
// Optical leak: light reaching the photodiode without going through the finger
// (most of the red one), in 16 counts per LED current unit, set by the client
// (ppg_set_leak()) or measured with the ring off the finger (ppg_measure_leak())
static uint16_t spo2_leak[2];
static uint8_t leak_polls;           // > 0: leak measurement running
#define LEAK_POLLS 30                // 3 s of locked channels
static int32_t dark_avg = 259000;    // dark phase reading (running average)
static uint16_t spo2_ratio_x1000, spo2_x10;
static uint8_t spo2_corr;

static bool write_table(const uint8_t (*table)[2], unsigned count)
{
    bool ok = true;
    for (unsigned i = 0; i < count; i++) {
        ok &= hx3605_write_reg(table[i][0], table[i][1]);
    }
    return ok;
}

bool ppg_start(void)
{
    // Wake up and chip check, as the stock firmware (0x07FC80B8)
    uint8_t id = 0;
    for (int i = 0; i < 10 && id != 0x27; i++) {
        hx3605_write_reg(HX3605_REG_SLEEP, 0x00);
        delay_ms(5);
        if (!hx3605_read_regs(HX3605_REG_CHIP_ID, &id, 1)) {
            id = 0;
        }
    }
    if (id != 0x27) {
        return false;
    }
    fs_t0 = lld_evt_time_get();
    fs_count = 0;
    fs_x100 = FS_DEFAULT_X100;
    ppg_set_led(PPG_LED_GREEN); // selected by ppg_set_mode() below
    return ppg_set_mode(PPG_MODE_HR);
}

// ---- Automatic gain control (vendor hx3918_hrs_agc.c, PPG_hrs_Calibration_Routine) ----
#define AGC_INIT_CURRENT 64     // HRS_CAL_INIT_LED
#define AGC_MAX_CURRENT  200    // green_led_max_init
#define AGC_IDAC_OFFSET  8      // HR_GREEN_AGC_OFFSET
#define AGC_ON_TIME      64     // 128 us
#define AGC_LOW          150000 // raw LED reading range
#define AGC_HIGH         350000
#define AGC_RECAL_LOW    100000 // out of this range for AGC_RECAL_POLLS: recalibrate
#define AGC_RECAL_HIGH   400000
#define AGC_RECAL_POLLS  10     // 1 s

static struct {
    bool enabled;
    uint8_t state, led, current, led_idac, amb_idac, wait, out_count;
} agc = {.enabled = true, .led = PPG_LED_GREEN};

// ---- Heart rate (ppg_hr.c) and sample rate ----

void ppg_set_motion(uint8_t level)
{
    motion_level = level;
}

static void hr_restart(void)
{
    ppg_hr_reset();
    hr_bpm_x10 = 0;
    hr_confidence = 0;
}

static void fs_update(uint32_t led_samples)
{
    uint32_t elapsed = (lld_evt_time_get() - fs_t0) & 0x07FFFFFF;
    fs_count += led_samples;
    if (elapsed >= FS_MIN_SLOTS) {
        uint32_t fs = (uint32_t)((uint64_t)fs_count * 160000 / elapsed); // 1 slot = 1/1600 s
        if (fs > 500 && fs < 10000) {
            fs_x100 = (uint16_t)fs;
        }
    }
    if (elapsed >= FS_MAX_SLOTS) {
        fs_t0 = lld_evt_time_get();
        fs_count = 0;
    }
}

// Raw phase readings (vendor read_data_packet()): ps[2] LED (green); ambient (dark):
// ps[3] in heart-rate mode, ps[0] in SpO2 mode (ps[1] red, ps[3] IR)
static int32_t raw_phase[4];
#define AMB_PHASE     (mode == PPG_MODE_SPO2 ? 0 : 3)
#define AMB_IDAC_REG  (mode == PPG_MODE_SPO2 ? 0x2c : 0x2f)

static void read_phases(void)
{
    uint8_t b[12];
    if (hx3605_read_regs(0x02, b, 12)) {
        for (int i = 0; i < 4; i++) {
            raw_phase[i] = b[3 * i] | (b[3 * i + 1] << 8) | ((int32_t)b[3 * i + 2] << 16);
        }
    }
}

static void agc_apply(void)
{
    hx3605_write_reg(0x28, agc.current);
    hx3605_write_reg(0x2e, agc.led_idac);
    hx3605_write_reg(AMB_IDAC_REG, agc.amb_idac);
}

void ppg_set_led(uint8_t led)
{
    agc.led = led;
    hr_restart();
    if (led == PPG_LED_OFF) {
        agc.current = 0;
        hx3605_write_reg(0x28, 0);
        agc.state = PPG_AGC_OFF;
        return;
    }
    // phase 2 LED driver; phase 3: none (ambient) in heart-rate mode, IR in SpO2 mode
    hx3605_write_reg(0x2b, led | (mode == PPG_MODE_SPO2 ? LDR_IR_PHASE3 : 0));
    agc.state = agc.enabled ? PPG_AGC_START : PPG_AGC_OFF;
    if (!agc.enabled) {
        hx3605_write_reg(0x28, agc.current);
    }
}

void ppg_agc_enable(bool enable)
{
    agc.enabled = enable;
    if (!enable) {
        agc.state = PPG_AGC_OFF;
        // keep the current settings, let the client change them
        hx3605_read_regs(0x28, &agc.current, 1);
    } else if (agc.led != PPG_LED_OFF) {
        agc.state = PPG_AGC_START;
    }
}

// One step every 200 ms (2 polls: 5 samples at 25 Hz settle after a change)
static void agc_step(void)
{
    if (agc.wait) {
        agc.wait--;
        return;
    }
    int32_t led = raw_phase[2], amb = raw_phase[AMB_PHASE];
    switch (agc.state) {
    case PPG_AGC_START:
        hr_restart();
        agc.current = AGC_INIT_CURRENT;
        agc.led_idac = 0;
        agc.amb_idac = 0;
        hx3605_write_reg(0x1e, AGC_ON_TIME);
        hx3605_write_reg(AMB_PHASE == 3 ? 0x1f : 0x1c, AGC_ON_TIME);
        agc_apply();
        agc.state = PPG_AGC_CURRENT;
        break;
    case PPG_AGC_CURRENT:
        agc.amb_idac = amb > 280000 ? (amb - 280000) / 10000 : 0;
        if (led > amb + 128) {
            int32_t step = (led - amb) / AGC_INIT_CURRENT; // counts per current unit
            int32_t current = 5000 * (AGC_IDAC_OFFSET + agc.amb_idac) / (step ? step : 1);
            agc.current = current > AGC_MAX_CURRENT ? AGC_MAX_CURRENT : (current < 1 ? 1 : current);
        } else {
            agc.current = AGC_MAX_CURRENT;
        }
        agc.led_idac = AGC_IDAC_OFFSET >> 1;
        agc_apply();
        agc.state = PPG_AGC_OFFSET;
        break;
    case PPG_AGC_OFFSET:
        if (led > AGC_HIGH && agc.led_idac <= 120) {
            agc.led_idac++;
        } else if (led < AGC_LOW && agc.led_idac >= 1) {
            agc.led_idac--;
        } else {
            agc.state = PPG_AGC_LOCKED;
            agc.out_count = 0;
            break;
        }
        hx3605_write_reg(0x2e, agc.led_idac);
        break;
    case PPG_AGC_LOCKED:
        if (led < AGC_RECAL_LOW || led > AGC_RECAL_HIGH) {
            if (++agc.out_count >= AGC_RECAL_POLLS / 2) {
                agc.state = PPG_AGC_START;
            }
        } else {
            agc.out_count = 0;
        }
        return; // no wait when locked
    default:
        return;
    }
    agc.wait = 1;
}

// ---- SpO2 mode: red and IR channels ----

static void spo2_restart(void)
{
    ppg_spo2_reset();
    spo2_ratio_x1000 = 0;
    spo2_corr = 0;
    spo2_x10 = 0;
}

static void spo2_channel_write(struct spo2_ch *c)
{
    hx3605_write_reg(c->cur_reg, c->current);
    hx3605_write_reg(c->idac_reg, c->idac);
}

bool ppg_set_mode(uint8_t new_mode)
{
    mode = new_mode == PPG_MODE_SPO2 ? PPG_MODE_SPO2 : PPG_MODE_HR;
    bool ok = write_table(stock_hr_setup, sizeof(stock_hr_setup) / sizeof(stock_hr_setup[0]));
    if (mode == PPG_MODE_SPO2) {
        ok &= write_table(spo2_setup, sizeof(spo2_setup) / sizeof(spo2_setup[0]));
        for (int i = 0; i < 2; i++) {
            // start with the offset of a typical ring (raw readings settle in < 1 s)
            spo2_ch[i].idac = i == 0 ? 14 : 6;
            spo2_ch[i].locked = false;
            spo2_ch[i].before = 0;
            spo2_channel_write(&spo2_ch[i]);
        }
    }
    leak_polls = 0;
    fifo_first_read = true;
    spo2_restart();
    ppg_set_led(agc.led); // phase 2/3 LED select, restarts the gain control and heart rate
    return ok;
}

void ppg_set_spo2_currents(uint8_t red, uint8_t ir)
{
    spo2_ch[0].current = red ? red : SPO2_RED_CURRENT;
    spo2_ch[1].current = ir ? ir : SPO2_IR_CURRENT;
    if (mode == PPG_MODE_SPO2) {
        for (int i = 0; i < 2; i++) {
            spo2_ch[i].locked = false;
            spo2_channel_write(&spo2_ch[i]);
        }
        spo2_restart();
    }
}

void ppg_set_leak(uint16_t red, uint16_t ir)
{
    spo2_leak[0] = red;
    spo2_leak[1] = ir;
    leak_polls = 0;
}

void ppg_measure_leak(void)
{
    leak_polls = mode == PPG_MODE_SPO2 ? 1 : 0;
}

// Offset DAC control of a red/IR channel, every 200 ms: steps of the measured size
// towards RAW_TARGET when out of RAW_LOW-RAW_HIGH, then locked until out of
// RAW_RECAL_LOW-RAW_RECAL_HIGH for 0.5 s. The LED current changes only if the offset
// DAC reaches its limits. Any change restarts the SpO2 window.
static void spo2_channel_step(struct spo2_ch *c)
{
    int32_t raw = raw_phase[c->phase];
    bool valid = raw > RAW_CLIP_LOW && raw < RAW_CLIP_HIGH;
    if (c->before && valid) {
        int32_t k = (c->before - raw) / c->step;
        if (k > 100000 && k < 200000) {
            idac_counts = (3 * idac_counts + k) / 4;
        }
    }
    c->before = 0;

    int delta;
    if (raw >= RAW_CLIP_HIGH) {
        delta = 3;
    } else if (raw <= RAW_CLIP_LOW) {
        delta = -2;
    } else if (c->locked) {
        if (raw < RAW_RECAL_LOW || raw > RAW_RECAL_HIGH) {
            if (++c->out_count >= 5) {
                c->locked = false;
            }
        } else {
            c->out_count = 0;
        }
        return;
    } else if (raw < RAW_LOW || raw > RAW_HIGH) {
        int32_t d = raw - RAW_TARGET;
        delta = (d + (d > 0 ? idac_counts / 2 : -idac_counts / 2)) / idac_counts;
        if (!delta) {
            delta = d > 0 ? 1 : -1;
        }
    } else {
        c->locked = true;
        c->out_count = 0;
        return;
    }
    c->locked = false;
    int idac = c->idac + delta;
    if (idac < 0) {
        idac = 0;
        if (raw <= RAW_CLIP_LOW && c->current < 100) { // too little light
            c->current *= 2;
            hx3605_write_reg(c->cur_reg, c->current);
        }
    } else if (idac > 255) {
        idac = 255;
        if (c->current > 1) { // too much light
            c->current /= 2;
            hx3605_write_reg(c->cur_reg, c->current);
        }
    }
    if (idac != c->idac) {
        if (valid) {
            c->before = raw;
            c->step = (int8_t)(idac - c->idac);
        }
        c->idac = (uint8_t)idac;
        hx3605_write_reg(c->idac_reg, c->idac);
    }
    spo2_restart();
}

// Light that went through the finger = mean raw + this: offset DAC (light removed)
// - dark reading - optical leak
static int32_t spo2_offset(int i)
{
    return spo2_ch[i].idac * idac_counts - dark_avg - (int32_t)spo2_leak[i] * 16 * spo2_ch[i].current;
}

// Leak measurement (ring off the finger, in the dark): light per current unit once
// both channels are locked for LEAK_POLLS
static void leak_step(void)
{
    if (!spo2_ch[0].locked || !spo2_ch[1].locked) {
        leak_polls = 1;
        return;
    }
    if (++leak_polls < LEAK_POLLS) {
        return;
    }
    int32_t raw[2];
    ppg_spo2_means(&raw[0], &raw[1]);
    for (int i = 0; i < 2; i++) {
        int32_t light = raw[i] + spo2_ch[i].idac * idac_counts - dark_avg;
        spo2_leak[i] = light > 0 ? (uint16_t)(light / 16 / spo2_ch[i].current) : 0;
    }
    leak_polls = 0;
}

void ppg_stop(void)
{
    write_table(stock_off, sizeof(stock_off) / sizeof(stock_off[0]));
}

uint8_t ppg_read(uint8_t packet[PPG_PACKET_MAX])
{
    static uint8_t seq = 0;
    uint8_t count = 0;
    uint8_t n = 0;

    if (!hx3605_read_regs(HX3605_REG_FIFO_COUNT, &count, 1)) {
        return 0;
    }
    // As the reference hx3605_read_fifo_data() (registers + 0x10): leave one
    // sample in the FIFO, drop the first one after a reset
    if (count >= 2 && count <= HX3605_FIFO_DEPTH) {
        if (fifo_first_read) {
            uint8_t dummy[3];
            hx3605_read_regs(HX3605_REG_FIFO_DATA, dummy, 3);
            fifo_first_read = false;
        }
        n = count - 1;
        if (n > PPG_MAX_SAMPLES) {
            n = PPG_MAX_SAMPLES;
        }
        if (!hx3605_read_regs(HX3605_REG_FIFO_DATA, &packet[PPG_HEADER], 3 * n)) {
            n = 0;
        }
    }
    if (count >= HX3605_FIFO_DEPTH) {
        // FIFO full (samples lost): reset it, as the stock firmware (0x07FC85EA)
        uint8_t ctrl = mode == PPG_MODE_SPO2 ? 0x80 : 0x83;
        hx3605_read_regs(HX3605_REG_FIFO_CTRL, &ctrl, 1);
        hx3605_write_reg(HX3605_REG_FIFO_CTRL, 0x00);
        delay_ms(5);
        hx3605_write_reg(HX3605_REG_FIFO_CTRL, ctrl);
        fifo_first_read = true;
    }
    // LED channel samples -> sample rate, heart rate; red/IR -> SpO2. Heart-rate mode:
    // phase flag 1 = LED channel (green - dark). SpO2 mode: flag = phase (0 dark, 1 red,
    // 2 green, 3 IR, in this order in each frame). While the hand moves, the samples
    // are dropped: the pulse is lost in the motion artifacts, and they would spoil
    // the results for the next 8 s (their window)
    static int32_t last_dark, last_red;
    uint32_t led_samples = 0;
    bool settled = agc.state == PPG_AGC_LOCKED || agc.state == PPG_AGC_OFF;
    bool moving = motion_level > PPG_MOTION_MAX;
    bool spo2_ready = mode == PPG_MODE_SPO2 && spo2_ch[0].locked && spo2_ch[1].locked;
    if (moving) {
        hr_restart();
        spo2_restart();
    }
    for (int k = 0; k < n; k++) {
        const uint8_t *p = &packet[PPG_HEADER + 3 * k];
        uint32_t raw = p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16);
        uint8_t flag = (raw >> 21) & 3;
        int32_t v = raw & 0x1FFFFF;
        if (v & 0x100000) {
            v -= 0x200000;
        }
        if (mode == PPG_MODE_SPO2) {
            if (flag == 0) {
                last_dark = v;
                dark_avg += (v - dark_avg) / 16;
                continue;
            } else if (flag == 1) {
                last_red = v;
                continue;
            } else if (flag == 3) {
                if (spo2_ready && !moving) {
                    ppg_spo2_add(last_red, v);
                }
                continue;
            }
            v -= last_dark; // green
        } else if (flag != 1) {
            continue;
        }
        led_samples++;
        if (settled && !moving && agc.led != PPG_LED_OFF) {
            ppg_hr_add(v);
        }
    }
    fs_update(led_samples);
    if (++hr_polls >= 10) { // ~1 s
        hr_polls = 0;
        ppg_hr_update(fs_x100, &hr_bpm_x10, &hr_confidence);
        if (spo2_ready) {
            ppg_spo2_compute(fs_x100, spo2_offset(0), spo2_offset(1),
                             &spo2_ratio_x1000, &spo2_corr, &spo2_x10);
        }
    }

    read_phases();
    if (agc.enabled) {
        agc_step();
        if (mode == PPG_MODE_SPO2 && (hr_polls & 1)) { // every 200 ms
            spo2_channel_step(&spo2_ch[0]);
            spo2_channel_step(&spo2_ch[1]);
        }
    }
    if (leak_polls) {
        leak_step();
    }
    packet[0] = seq++;
    packet[1] = n;
    packet[2] = count;
    packet[3] = agc.current;
    packet[4] = agc.led_idac;
    packet[5] = agc.amb_idac;
    packet[6] = agc.state | (agc.led & 0xF0);
    for (int i = 0; i < 3; i++) {
        packet[7 + i] = raw_phase[2] >> (8 * i);
        packet[10 + i] = raw_phase[AMB_PHASE] >> (8 * i);
    }
    packet[13] = hr_bpm_x10;
    packet[14] = hr_bpm_x10 >> 8;
    packet[15] = hr_confidence;
    packet[16] = fs_x100;
    packet[17] = fs_x100 >> 8;
    packet[18] = motion_level;
    bool spo2 = mode == PPG_MODE_SPO2;
    packet[19] = mode | (spo2 && spo2_ch[0].locked ? 0x10 : 0) | (spo2 && spo2_ch[1].locked ? 0x20 : 0)
                 | (leak_polls ? 0x80 : 0);
    packet[20] = spo2_ch[0].current;
    packet[21] = spo2_ch[0].idac;
    packet[22] = spo2_ch[1].current;
    packet[23] = spo2_ch[1].idac;
    packet[24] = spo2_x10;
    packet[25] = spo2_x10 >> 8;
    packet[26] = spo2_corr;
    packet[27] = spo2_ratio_x1000;
    packet[28] = spo2_ratio_x1000 >> 8;
    packet[29] = spo2_leak[0];
    packet[30] = spo2_leak[0] >> 8;
    packet[31] = spo2_leak[1];
    packet[32] = spo2_leak[1] >> 8;
    packet[33] = idac_counts / 16;
    packet[34] = (idac_counts / 16) >> 8;
    return PPG_HEADER + 3 * n;
}
