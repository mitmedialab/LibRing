#include "gpio.h"
#include "leds.h"

static led_gpios theLedGpiosLow[] = {
        {GPIO_PORT_2, GPIO_PIN_1},
        {GPIO_PORT_1, GPIO_PIN_3},
        {GPIO_PORT_2, GPIO_PIN_0},
        {GPIO_PORT_1, GPIO_PIN_2},
        {GPIO_PORT_1, GPIO_PIN_0},
        {GPIO_PORT_1, GPIO_PIN_1},
        {GPIO_PORT_0, GPIO_PIN_2},
};

static led_gpios theLedGpiosHigh[] = {
        {GPIO_PORT_2, GPIO_PIN_8},
        {GPIO_PORT_2, GPIO_PIN_7},
        {GPIO_PORT_2, GPIO_PIN_6},
        {GPIO_PORT_2, GPIO_PIN_5},
        {GPIO_PORT_2, GPIO_PIN_2},
        {GPIO_PORT_2, GPIO_PIN_9},
};

uint8_t LED_overlay[6];
uint8_t LED_indicators;

// 7-segment patterns of 0-9 and A-F (b, d lowercase). Segment bits:
// bit0=A (top), bit1=F (top left), bit2=B (top right), bit3=G (middle),
// bit4=E (bottom left), bit5=C (bottom right), bit6=D (bottom)
const uint8_t numbers[] = {0x77,0x24,0x5D,0x6D,0x2E,0x6B,0x7B,0x25,0x7F,0x6F,0x3F,0x7A,0x53,0x7C,0x5B,0x1B};

uint8_t LED_hex_digit(uint8_t value) {
    return numbers[value & 0xF];
}

void LED_Pin_Config(bool enable) {
    if (enable) {
        for (int i = 0; i < (sizeof(theLedGpiosLow)/sizeof(led_gpios)); ++i )
            GPIO_ConfigurePin(theLedGpiosLow[i].port, theLedGpiosLow[i].pin, OUTPUT, PID_GPIO, 0);
        for (int i = 0; i < (sizeof(theLedGpiosHigh)/sizeof(led_gpios)); ++i )
            GPIO_ConfigurePin(theLedGpiosHigh[i].port, theLedGpiosHigh[i].pin, OUTPUT, PID_GPIO, 1);
    } else {
        for (int i = 0; i < (sizeof(theLedGpiosLow)/sizeof(led_gpios)); ++i )
            GPIO_ConfigurePin(theLedGpiosLow[i].port, theLedGpiosLow[i].pin, INPUT_PULLDOWN, PID_GPIO, 0);
        for (int i = 0; i < (sizeof(theLedGpiosHigh)/sizeof(led_gpios)); ++i )
            GPIO_ConfigurePin(theLedGpiosHigh[i].port, theLedGpiosHigh[i].pin, INPUT_PULLDOWN, PID_GPIO, 1);
    }
}

void LED_Buff_setInt(uint32_t inputNum, unsigned char *LED_Buf, int lenInt) {
    uint8_t v19[5] = {0};
    int i;
    uint32_t v4 = 1;
    if (lenInt > 5) lenInt = 5;
    for (i = lenInt - 1; i >= 0; i--) {
        v19[i] = (inputNum / v4) % 10;
        v4 *= 10;
    }
    for (i = 0; i < lenInt; i++) {
        LED_Buf[i] = numbers[v19[i]];
    }
}

void LED_write(unsigned char *LED_Buffer, int line) {
    uint8_t prev_line = (line + 5) % 6;
    uint8_t bits = LED_Buffer[line] | LED_overlay[line] | (line == 5 ? LED_indicators : 0);
    GPIO_SetActive( theLedGpiosHigh[prev_line].port, theLedGpiosHigh[prev_line].pin );
        for(int i =0;i<7;i++)
    {
            if((bits >> i) & 1)
                    GPIO_SetActive( theLedGpiosLow[i].port, theLedGpiosLow[i].pin );
            else
                    GPIO_SetInactive( theLedGpiosLow[i].port, theLedGpiosLow[i].pin );
    }
    GPIO_SetInactive( theLedGpiosHigh[line].port, theLedGpiosHigh[line].pin );
}