#include "hx3605.h"

#include <string.h>

#include "arch.h"
#include "i2c.h"
#include "accelerometer.h"

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
