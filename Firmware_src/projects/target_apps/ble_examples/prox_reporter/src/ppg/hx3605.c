#include "hx3605.h"

#include <string.h>

#include "arch.h"
#include "i2c.h"
#include "accelerometer.h"
#include "lld_evt.h"
#include "ppg_hr.h"

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
    fifo_first_read = true;
    fs_t0 = lld_evt_time_get();
    fs_count = 0;
    fs_x100 = FS_DEFAULT_X100;
    if (!write_table(stock_hr_setup, sizeof(stock_hr_setup) / sizeof(stock_hr_setup[0]))) {
        return false;
    }
    ppg_set_led(PPG_LED_GREEN);
    return true;
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

// Raw phase readings (vendor read_data_packet()): ps[2] LED (green), ps[3] ambient
static int32_t raw_phase[4];

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
    hx3605_write_reg(0x2f, agc.amb_idac);
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
    hx3605_write_reg(0x2b, led); // phase 2 LED driver (phase 3: none = ambient)
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
    int32_t led = raw_phase[2], amb = raw_phase[3];
    switch (agc.state) {
    case PPG_AGC_START:
        hr_restart();
        agc.current = AGC_INIT_CURRENT;
        agc.led_idac = 0;
        agc.amb_idac = 0;
        hx3605_write_reg(0x1e, AGC_ON_TIME);
        hx3605_write_reg(0x1f, AGC_ON_TIME);
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
        uint8_t ctrl = 0x83;
        hx3605_read_regs(HX3605_REG_FIFO_CTRL, &ctrl, 1);
        hx3605_write_reg(HX3605_REG_FIFO_CTRL, 0x00);
        delay_ms(5);
        hx3605_write_reg(HX3605_REG_FIFO_CTRL, ctrl);
        fifo_first_read = true;
    }
    // LED channel samples (phase flag 1) -> sample rate, heart rate
    uint32_t led_samples = 0;
    bool settled = agc.state == PPG_AGC_LOCKED || agc.state == PPG_AGC_OFF;
    for (int k = 0; k < n; k++) {
        const uint8_t *p = &packet[PPG_HEADER + 3 * k];
        uint32_t raw = p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16);
        if (((raw >> 21) & 3) != 1) {
            continue;
        }
        led_samples++;
        int32_t v = raw & 0x1FFFFF;
        if (v & 0x100000) {
            v -= 0x200000;
        }
        if (settled && agc.led != PPG_LED_OFF) {
            ppg_hr_add(v);
        }
    }
    fs_update(led_samples);
    if (++hr_polls >= 10) { // ~1 s
        hr_polls = 0;
        ppg_hr_compute(fs_x100, &hr_bpm_x10, &hr_confidence);
    }

    read_phases();
    if (agc.enabled) {
        agc_step();
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
        packet[10 + i] = raw_phase[3] >> (8 * i);
    }
    packet[13] = hr_bpm_x10;
    packet[14] = hr_bpm_x10 >> 8;
    packet[15] = hr_confidence;
    packet[16] = fs_x100;
    packet[17] = fs_x100 >> 8;
    packet[18] = motion_level;
    return PPG_HEADER + 3 * n;
}

// ---- Probe ----

// true if a device ACKs a 1-byte read at addr: a read without a register write
// first, so that no register of an unknown chip is changed
static bool i2c_device_present(uint16_t addr)
{
    i2c_abort_t abort_code;
    uint8_t byte;
    uint16_t previous = i2c_get_target_address();
    i2c_set_target(addr);
    i2c_master_receive_buffer_sync(&byte, 1, &abort_code, I2C_F_NONE);
    i2c_set_target(previous);
    return abort_code == I2C_ABORT_NONE;
}

void ppg_probe(uint8_t out[PPG_PROBE_LEN])
{
    memset(out, 0, PPG_PROBE_LEN);

    for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_device_present(addr)) {
            out[1 + (addr - 0x08) / 8] |= 1 << ((addr - 0x08) % 8);
        }
    }

    uint8_t flags = 0;
    uint8_t id = 0xFF;
    if (!hx3605_read_regs(HX3605_REG_CHIP_ID, &id, 1)) {
        id = 0xFF;
        flags |= 1 << 0;
    }
    out[16] = id;

    // Wake up as the reference driver and the stock firmware do
    if (!hx3605_write_reg(HX3605_REG_SLEEP, 0x00)) {
        flags |= 1 << 1;
    }
    arch_asm_delay_us(5000);

    id = 0xFF;
    if (!hx3605_read_regs(HX3605_REG_CHIP_ID, &id, 1)) {
        id = 0xFF;
        flags |= 1 << 2;
    }
    out[17] = id;

    if (!hx3605_read_regs(0x01, &out[19], 8)) {
        flags |= 1 << 3;
    }

    // Back to sleep (the reference driver's hx3605_ppg_off() also clears 0x22 and
    // 0x10, which this probe doesn't change)
    if (!hx3605_write_reg(HX3605_REG_SLEEP, 0x01)) {
        flags |= 1 << 4;
    }
    out[18] = flags;

    out[27] = accel_cmd_whoami();
    out[28] = (uint8_t)i2c_get_target_address();
    out[0] = 0x50;
}
