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

// Matrix scan (timer0 interrupt): one line at a time, bit i = segment i
void LED_line_on(int line, uint8_t bits);
void LED_segments(uint8_t bits);
void LED_line_off(int line);

// LED matrix: 6 lines (theLedGpiosHigh) x 7 segments (theLedGpiosLow). Lines 0-4 are
// the 5 digits; line 5 (P2_9) holds the indicators on the outside of the ring.
// Extra LEDs lit on top of the display, per line (bit i = segment i), for tests: see
// the [0xF6, line, mask] command (user_ppg_config()).
extern uint8_t LED_overlay[6];

// Outside indicators (line 5, P2_9), lit with the display (not when it is off)
#define LED_IND_HEART (1 << 4) // red heart (segment pin P1_0)
#define LED_IND_STEPS (1 << 6) // green steps (segment pin P0_2)
extern uint8_t LED_indicators;
// Charge indicators (LED_CHARGE_TICKS), also with the display off: red heart while
// charging, green steps when charged
extern uint8_t LED_charge_indicators;

// Brightness: each line has LED_TICKS_PER_LINE timer ticks of 0.25 ms (1.5 ms per
// line, 111 Hz frames); the LEDs are on during LED_ON_TICKS of them (50 % of the line:
// 8.3 % duty), the charge indicators during LED_CHARGE_TICKS (1/6 x 4/6 = 11 % duty)
#define LED_TICKS_PER_LINE 6
#define LED_ON_TICKS       3
#define LED_CHARGE_TICKS   4
#endif