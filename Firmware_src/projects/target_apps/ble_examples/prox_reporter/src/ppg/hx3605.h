#ifndef _HX3605_H_
#define _HX3605_H_

#include <stdbool.h>
#include <stdint.h>

// PPG sensor (heart rate / SpO2): TYHX HX3918 (chip ID 0x27 in register 0x00, I2C
// address 0x44; not the HX3605 = 0x25 of issue #3), on the I2C bus shared with the
// accelerometer (P2_3/P2_4). Vendor driver (register map, LED gain control):
// github.com/HK-bruc1/jl_ac701N_ear SDK/apps/common/device/sensor/hr_sensor/hx3918/
// LEDs on the SR08: LDR0 green, LDR1 IR, LDR2 red. Main registers: 0x16 phase enable,
// 0x18/0x19 period (32000 / Hz), 0x1c-0x1f LED on-time per phase, 0x23 LED enables,
// 0x26-0x29 LED current per phase (0-255 = 0-200 mA), 0x2a/0x2b LED driver select.
#define HX3605_I2C_ADDR     0x44
#define HX3605_REG_CHIP_ID  0x00
#define HX3605_REG_SLEEP    0x01 // 0x00: awake, 0x01: sleep (reference driver)

// Register access. The I2C target address is switched to the HX3605 and restored
// afterwards (the accelerometer driver relies on it). The I2C controller must be
// initialized (accel_init()). Return false on an I2C error.
bool hx3605_read_regs(uint8_t reg, uint8_t *buf, uint8_t len);
bool hx3605_write_reg(uint8_t reg, uint8_t value);

// ---- Streaming ----
// The setup is the one of the stock firmware (Stock_V160.bin), recorded by emulating
// its heart-rate mode (25 Hz, FIFO). It leaves the green LED (phase 2 current, 0x28)
// at 0: the stock firmware's gain control raises it during a measurement. ~2 (1.6 mA)
// suits a finger (0x08 already saturates).
#define HX3605_REG_FIFO_CTRL  0x3d // FIFO enable + data select (0x83 in HR mode)
#define HX3605_REG_FIFO_COUNT 0x40
#define HX3605_REG_FIFO_DATA  0x41 // 3 bytes per sample
#define HX3605_FIFO_DEPTH     64

// Wakes the chip, checks its ID (0x27), starts the stock heart-rate setup and the
// automatic gain control with the green LED. Returns false if the chip doesn't answer.
bool ppg_start(void);

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

// Notification packet, read every ~100 ms (25 Hz: ~2-3 samples per phase):
//   [0]      sequence number (wraps)
//   [1]      n, number of FIFO samples in this packet
//   [2]      FIFO count register before reading
//   [3]      LED current (0x28)       [4] LED offset DAC (0x2e)
//   [5]      ambient offset DAC (0x2f)
//   [6]      bits 3:0 AGC state (enum ppg_agc_state), bits 7:4 LED (PPG_LED_* >> 4)
//   [7..9]   raw LED reading (phase 2, registers 0x08-0x0a), unsigned, little endian
//   [10..12] raw ambient reading (phase 3, registers 0x0b-0x0d)
//   [13..14] heart rate in 1/10 bpm, computed on the ring; 0: no clear pulse, moving,
//            or not enough data yet (5 s), see ppg_hr_update()
//   [15]     heart rate confidence 0-100 (>= 50: clear pulse; 0: not enough data)
//   [16..17] measured sample rate per channel in 1/100 Hz
//   [18]     motion level (accelerometer, 8 mg units, see ppg_set_motion())
//   [19..]   n FIFO samples (register 0x41), 3 bytes each, raw, little endian
//            (bits 20:0 signed value, bits 22:21 phase flag: 1 = LED channel)
#define PPG_HEADER      19
#define PPG_MAX_SAMPLES 16
#define PPG_PACKET_MAX  (PPG_HEADER + 3 * PPG_MAX_SAMPLES)

enum ppg_agc_state { PPG_AGC_OFF, PPG_AGC_START, PPG_AGC_CURRENT, PPG_AGC_OFFSET, PPG_AGC_LOCKED };

// Fills packet, returns its length (0 on an I2C error)
uint8_t ppg_read(uint8_t packet[PPG_PACKET_MAX]);

#endif // _HX3605_H_
