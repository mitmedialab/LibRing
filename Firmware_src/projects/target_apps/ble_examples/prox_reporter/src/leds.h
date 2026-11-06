#ifndef _LEDS_H_
#define _LEDS_H_

#include "gpio.h"

typedef struct led_gpios
{
        GPIO_PORT port;
        GPIO_PIN pin;
}led_gpios;

void LED_Pin_Config(bool enable);

void LED_Buff_setInt(uint32_t inputNum, unsigned char *LED_Buf, int lenInt);

// 7-segment pattern of a hex digit (low 4 bits of value)
uint8_t LED_hex_digit(uint8_t value);

void LED_write(unsigned char *LED_Buffer, int line);
#endif