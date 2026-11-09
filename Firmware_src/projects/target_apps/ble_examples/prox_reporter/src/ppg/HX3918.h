#ifndef _HX3918_H_
#define _HX3918_H_

#include <stdbool.h>
#include <stdint.h>

// PPG sensor (heart rate / SpO2): TYHX HX3918 (chip ID 0x27 in register 0x00, I2C
// address 0x44; not the HX3605 = 0x25 of issue #3), on the I2C bus shared with the
// accelerometer (P2_3/P2_4). Vendor driver (register map, LED gain control):
// github.com/HK-bruc1/jl_ac701N_ear SDK/apps/common/device/sensor/hr_sensor/hx3918/
// LEDs on the SR08: LDR0 green, LDR1 IR, LDR2 red. Main registers: 0x16 phase enable,
// 0x18/0x19 period (32000 / Hz), 0x1c-0x1f LED on-time per phase, 0x23 LED enables,
// 0x26-0x29 LED current per phase (0-255 = 0-200 mA), 0x2a/0x2b LED driver select.
#define HX3918_I2C_ADDR     0x44
#define HX3918_REG_CHIP_ID  0x00
#define HX3918_REG_SLEEP    0x01 // 0x00: awake, 0x01: sleep (reference driver)

// Register access. The I2C target address is switched to the HX3918 and restored
// afterwards (the accelerometer driver relies on it). The I2C controller must be
// initialized (accel_init()). Return false on an I2C error.
bool hx3918_read_regs(uint8_t reg, uint8_t *buf, uint8_t len);
bool hx3918_write_reg(uint8_t reg, uint8_t value);

// ---- Streaming ----
// The setup is the one of the stock firmware (Stock_V160.bin), recorded by emulating
// its heart-rate mode (25 Hz, FIFO). It leaves the green LED (phase 2 current, 0x28)
// at 0: the stock firmware's gain control raises it during a measurement. ~2 (1.6 mA)
// suits a finger (0x08 already saturates).
#define HX3918_REG_FIFO_CTRL  0x3d // FIFO enable + data select (0x83 in HR mode)
#define HX3918_REG_FIFO_COUNT 0x40
#define HX3918_REG_FIFO_DATA  0x41 // 3 bytes per sample
#define HX3918_FIFO_DEPTH     64

// Wakes the chip, checks its ID (0x27), starts the stock heart-rate setup and the
// automatic gain control with the green LED (heart-rate mode). Returns false if the
// chip doesn't answer.
bool ppg_start(void);

// Heart-rate mode: green LED on phase 2, phase 3 dark, FIFO with green - dark.
// SpO2 mode: phase 0 dark, 1 red, 2 green (heart rate), 3 IR, FIFO with the 4 raw
// phases; red/IR offset DACs controlled by the ring; SpO2 computed (ppg_spo2.c).
#define PPG_MODE_HR   0
#define PPG_MODE_SPO2 1
bool ppg_set_mode(uint8_t mode);

// SpO2 mode: red and IR LED currents (0: default, 8 and 24)
void ppg_set_spo2_currents(uint8_t red, uint8_t ir);

// SpO2 mode: optical leak of the red and IR LEDs (light reaching the photodiode
// without going through the finger), in 16 raw counts per LED current unit, as
// measured by ppg_measure_leak() (kept by the client: lost when the ring restarts)
void ppg_set_leak(uint16_t red, uint16_t ir);

// SpO2 mode: measure the optical leak, with the ring OFF the finger, in the dark
// (~4 s; packet byte 19 bit 7 while running, result in bytes 29-32)
void ppg_measure_leak(void);

// LED of phase 2 (the FIFO LED channel): PPG_LED_GREEN/IR/RED (LED driver select,
// register 0x2b bits 7:4), or PPG_LED_OFF. Restarts the automatic gain control.
#define PPG_LED_OFF   0x00
#define PPG_LED_GREEN 0x10 // LDR0
#define PPG_LED_IR    0x20 // LDR1
#define PPG_LED_RED   0x40 // LDR2
void ppg_set_led(uint8_t led);

// Automatic gain control (as the vendor's hx3918_hrs_agc.c): bright LED (current up
// to 200 = 157 mA, 128 us pulses) with the offset DACs (0x2e LED, 0x2f ambient)
// keeping the raw LED reading in 150k-350k. Disable it to set registers by hand.
void ppg_agc_enable(bool enable);

// Motion level for the packets (the caller reads the accelerometer)
void ppg_set_motion(uint8_t level);
// Above this motion level the hand moves (25 x 8 = 200 mg; at rest <= ~100 mg): the
// heart rate restarts
#define PPG_MOTION_MAX 25

// Stock "off" setup: sampling off, chip asleep
void ppg_stop(void);

// Notification packet, read every ~100 ms (24.2 Hz: ~2-3 samples per phase):
//   [0]      sequence number (wraps)
//   [1]      n, number of FIFO samples in this packet
//   [2]      FIFO count register before reading
//   [3]      LED current (0x28)       [4] LED offset DAC (0x2e)
//   [5]      ambient offset DAC (0x2f, SpO2 mode: 0x2c)
//   [6]      bits 3:0 AGC state (enum ppg_agc_state), bits 7:4 LED (PPG_LED_* >> 4)
//   [7..9]   raw LED reading (phase 2, registers 0x08-0x0a), unsigned, little endian
//   [10..12] raw ambient reading (phase 3, registers 0x0b-0x0d; SpO2 mode: phase 0)
//   [13..14] heart rate in 1/10 bpm, computed on the ring; 0: no clear pulse, moving,
//            or not enough data yet (5 s), see ppg_hr_update()
//   [15]     heart rate confidence 0-100 (>= 50: clear pulse; 0: not enough data)
//   [16..17] measured sample rate per channel in 1/100 Hz
//   [18]     motion level (accelerometer, 8 mg units, see ppg_set_motion())
//   [19]     bits 3:0 mode (PPG_MODE_*), bit 4/5 red/IR offset locked, bit 7 leak
//            measurement running
//   [20] red current (0x27)  [21] red offset DAC (0x2d)
//   [22] IR current (0x29)   [23] IR offset DAC (0x2f)
//   [24..25] SpO2 in 1/10 % (0: no reliable value, see ppg_spo2.h)
//   [26]     red/IR pulse correlation x100 (SpO2 reported from PPG_SPO2_CORR_MIN)
//   [27..28] ratio R x1000 (0: not enough data)
//   [29..30] red optical leak, [31..32] IR (see ppg_set_leak(); 0: not calibrated)
//   [33..34] raw counts per offset DAC step / 16 (measured)
//   [35..]   n FIFO samples (register 0x41), 3 bytes each, raw, little endian
//            (bits 20:0 signed value, bits 22:21 phase flag: heart-rate mode 1 =
//            LED channel = green - dark; SpO2 mode: the phase, 0 dark, 1 red, 2 green,
//            3 IR)
#define PPG_HEADER      35
#define PPG_MAX_SAMPLES 16
#define PPG_PACKET_MAX  (PPG_HEADER + 3 * PPG_MAX_SAMPLES)

enum ppg_agc_state { PPG_AGC_OFF, PPG_AGC_START, PPG_AGC_CURRENT, PPG_AGC_OFFSET, PPG_AGC_LOCKED };

// Fills packet, returns its length (0 on an I2C error)
uint8_t ppg_read(uint8_t packet[PPG_PACKET_MAX]);

#endif // _HX3918_H_
