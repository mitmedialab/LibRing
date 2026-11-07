#ifndef _HX3605_H_
#define _HX3605_H_

#include <stdbool.h>
#include <stdint.h>

// PPG sensor (heart rate / SpO2): TYHX HX3605 variant, on the I2C bus shared with
// the accelerometer (P2_3/P2_4). The stock firmware expects chip ID 0x27 (register
// 0x00) at address 0x44; the HX3605 datasheet and TYHX reference driver say 0x25.
#define HX3605_I2C_ADDR     0x44
#define HX3605_REG_CHIP_ID  0x00
#define HX3605_REG_SLEEP    0x01 // 0x00: awake, 0x01: sleep (reference driver)

// Register access. The I2C target address is switched to the HX3605 and restored
// afterwards (the accelerometer driver relies on it). The I2C controller must be
// initialized (accel_init()). Return false on an I2C error.
bool hx3605_read_regs(uint8_t reg, uint8_t *buf, uint8_t len);
bool hx3605_write_reg(uint8_t reg, uint8_t value);

// ---- Probe (branch ppg, step 1) ----
#define PPG_PROBE_LEN 29

// Fills out[PPG_PROBE_LEN]:
//   [0]      0x50 ('P'): probe done
//   [1..15]  I2C bus scan, 1 bit per 7-bit address 0x08..0x77 (bit i of byte
//            1 + (addr - 8) / 8 = address addr), set if the address ACKs a 1-byte read
//   [16]     chip ID before waking the chip (0xFF + flag bit 0 if the read failed)
//   [17]     chip ID after writing 0x01 = 0x00 and waiting 5 ms
//   [18]     flags: bit 0 read before wake failed, bit 1 wake write failed,
//            bit 2 read after wake failed, bit 3 register dump failed,
//            bit 4 sleep write failed
//   [19..26] registers 0x01..0x08 after wake (burst read)
//   [27]     accelerometer WHO_AM_I after the probe (0x11 for the SC7A20)
//   [28]     I2C target address after the probe (0x18 expected)
// The chip is put back to sleep (0x01 = 0x01) at the end.
void ppg_probe(uint8_t out[PPG_PROBE_LEN]);

#endif // _HX3605_H_
