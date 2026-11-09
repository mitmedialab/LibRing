#include "rwip_config.h"
#include "gattc_task.h"
#include "gapc_task.h"
#include "user_periph_setup.h"
#include "wkupct_quadec.h"
#include "app_easy_msg_utils.h"
#include "gpio.h"
#include "app_security.h"
#include "user_proxr.h"
#include "arch.h"
#include "arch_api.h"
#include "lld_evt.h"
#include "app_task.h"
#include "app_proxr.h"

#include "timer0.h"
#include "timer0_2.h"
#if (BLE_SUOTA_RECEIVER)
#include "app_suotar.h"
#endif

#if defined (CFG_SPI_FLASH_ENABLE)
#include "spi_flash.h"
#endif

#include "arch_console.h"

#include "user_config.h"

#include "leds.h"

#include "./accelerometer/accelerometer.h"
#include "custs1_task.h"
#include "user_custs1_impl.h"

#include "fluid/fluid_5x25.h"
#include "ppg/hx3605.h"

#include "battery.h"

#include "adc.h"
#include "nvds.h"
#include "co_math.h"
#include "spi.h"


#if defined(__IS_SDK6_COMPILER_GCC__) && !defined(__clang__)
#pragma message("Please note that SDK6 GCC support will be deprecated in the next SDK6 release")
#endif

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



uint8_t LED_Buffer[6] = {0x00,0x00,0x00,0x00,0x00,0x00};

// Persistent 5x25 fluid simulation state
static fluid_5x25_t g_fluid;

// Cache last converted accel reading for use from timer ISR/display code
static volatile accel_data_t g_last_accel_mg = {0,0,0};

uint32_t counter_time = 0;
uint8_t counter_ms = 0;
uint8_t current_line = 0;
uint32_t realUnix = 0;
uint32_t lastTime = 0;

uint32_t led_value = 0;

uint8_t LED_Display_state = 0;
uint32_t turnOnTime = 0;

uint8_t whoami_res = 0; 


accel_sensitivity_t sens; 

static timer_hnd main_timer_hnd = EASY_TIMER_INVALID_TIMER; 
static timer_hnd button_hold_timer_hnd = EASY_TIMER_INVALID_TIMER;
static timer_hnd fluid_timer_hnd = EASY_TIMER_INVALID_TIMER;

typedef enum {
    DEFAULT,
    ACCEL_INIT,
    // ACCEL_CONFIG, 
    // ACCEL_SENS, 
    // WHOAMI, 
    // DISP_INFO, 
    
    FLUID_MODE, 
    BATT_MODE, 
    BT_INIT, 
    BT_NOTIF, 
    NUM_STATES
} user_state_t;

uint8_t user_state = 0;
bool user_run = false;  

bool has_timer_started = false; 

// true while a BLE client is subscribed to accel notifications: stream at every
// main timer tick, whatever the button state (the display modes are independent)
static bool accel_streaming = false;

// true while a firmware update (SUOTA) is in progress: the display and the
// accelerometer are paused so that nothing can slow down or break the update
static bool suota_active = false;

// What the display shows, changed by a long press (button held 3 s):
// fluid -> ring ID -> off -> fluid...
typedef enum {
    DISPLAY_FLUID,   // fluid simulation (at boot)
    DISPLAY_RING_ID, // the 2 hex digits of the ring name (LibRing_XX)
    DISPLAY_OFF,     // LEDs off, everything else runs (BLE, accelerometer)
} display_mode_t;
static display_mode_t display_mode = DISPLAY_FLUID;

// true once fluid_start_cb() has run: the display stays off during the first 5 s
static bool display_started = false;

// true while DISPLAY_OFF keeps the system awake (see display_next_mode())
static bool display_off_keeps_awake = false;

// Ring ID shown by DISPLAY_RING_ID, the same as in the BLE name (see ring_id())
static uint8_t g_ring_id = 0;

#define LONG_PRESS_TIME  300 // 3 s (10 ms units): change the display mode
#define RESET_PRESS_TIME 700 // 7 s more (10 s in total): reset the ring

// Display on, unless off by choice or paused (boot delay, firmware update)
static bool display_wanted(void)
{
    return display_started && !suota_active && display_mode != DISPLAY_OFF;
}

// The 2 hex digits on the 2 rightmost digits (LED_Buffer[0] is the leftmost)
static void show_ring_id(void)
{
    memset(LED_Buffer, 0x00, sizeof(LED_Buffer));
    LED_Buffer[3] = LED_hex_digit(g_ring_id >> 4);
    LED_Buffer[4] = LED_hex_digit(g_ring_id);
}

static void display_next_mode(void)
{
    switch (display_mode) {
    case DISPLAY_FLUID:
        display_mode = DISPLAY_RING_ID;
        show_ring_id(); // fluid_timer_cb() stops updating LED_Buffer
        break;
    case DISPLAY_RING_ID:
        display_mode = DISPLAY_OFF;
        // LED_GPIO_mode(0) also allows sleep, but the firmware always stays awake
        // with the display on (sleep loses the accelerometer setup, and this is
        // the tested setup): turn off the LEDs only
        if (!display_off_keeps_awake) {
            display_off_keeps_awake = true;
            arch_force_active_mode();
        }
        LED_GPIO_mode(0);
        break;
    default: // DISPLAY_OFF
        display_mode = DISPLAY_FLUID;
        if (display_wanted()) {
            LED_GPIO_mode(1);
        }
        if (display_off_keeps_awake) {
            display_off_keeps_awake = false;
            arch_restore_sleep_mode();
        }
        break;
    }
    arch_printf("Display mode: %d\r\n", display_mode);
}

// Button held RESET_PRESS_TIME after the long press: reset the ring
static void button_reset_timer_cb(void)
{
    button_hold_timer_hnd = EASY_TIMER_INVALID_TIMER;
    platform_reset(RESET_NO_ERROR);
}

// Button held LONG_PRESS_TIME: change the display mode (released before = no effect)
static void button_hold_timer_cb(void)
{
    display_next_mode();
    button_hold_timer_hnd = app_easy_timer(RESET_PRESS_TIME, button_reset_timer_cb);
}

void calcTime()
{
           uint32_t currentTime = lld_evt_time_get();
           if(currentTime == 0)
                   return;
           currentTime/=1000;
           if (currentTime < lastTime) {
               realUnix += (UINT32_MAX / 1000) + 1;
           }
           realUnix += (currentTime - lastTime);
           lastTime = currentTime;
}

void LED_GPIO_mode(uint8_t mode)
{
        arch_printf("LED_GPIO_mode: %i\r\n", mode);
        if(mode == 1)
        {
                if(LED_Display_state != 1)
                {
                        LED_Display_state = 1;
                        calcTime();
                        turnOnTime = realUnix;
                        LED_Pin_Config(true);
                        // Here we enable the Timer
                        arch_force_active_mode();
                        timer0_enable_irq();
                        timer0_start();
                }
        } else {
                if(LED_Display_state != 0)
                {
                        LED_Display_state = 0;
                        LED_Pin_Config(false);
                        // Here we disable the Timer
                        timer0_disable_irq();
                        timer0_stop();
                        arch_restore_sleep_mode();
                }
        }
}

void refreshMenu()
{
        //LED_Buff_setInt(counter_time, LED_Buffer, 5);
        calcTime();
        /*LED_Buff_setInt((realUnix % 86400) / 3600, LED_Buffer, 2);
        LED_Buff_setInt((realUnix % 3600) / 60, &LED_Buffer[3], 2);
        if((counter_time%2)==0)
                LED_Buffer[2] = 0x48;*/

        // --- Fluid display ---
        // Step the simulation with explicit normalized forces in [0,1].
        if (user_state == FLUID_MODE) {
            // Use cached accel (converted to mg) populated by main_timer_cb
            accel_data_t a = g_last_accel_mg;

            // Map accelerometer X into left_0_1 using two observed calibration poses (mg):
            //   left=0 -> x ~= -700
            //   left=1 -> x ~= 1400
            // left_0_1 = clamp01((x - x_lo) / (x_hi - x_lo))
            const int16_t x_lo = -700;
            const int16_t x_hi =  1400;
            const float denom_x = (float)(x_hi - x_lo);
            float left_0_1 = 0.50f;
            if (denom_x != 0.0f) {
                left_0_1 = ((float)a.x - (float)x_lo) / denom_x;
            }
            if (left_0_1 < 0.0f) left_0_1 = 0.0f;
            if (left_0_1 > 1.0f) left_0_1 = 1.0f;

            // Map accelerometer Z into down_0_1 using two observed calibration poses (mg):
            //   down=1 -> z ~= -560
            //   down=0 -> z ~= 1268
            // down_0_1 = clamp01((z_hi - z) / (z_hi - z_lo))
            const int16_t z_lo = -1400;
            const int16_t z_hi =  1400;
            const float denom_z = (float)(z_hi - z_lo);
            float down_0_1 = 0.50f;
            if (denom_z != 0.0f) {
                down_0_1 = ((float)z_hi - (float)a.z) / denom_z;
            }
            if (down_0_1 < 0.0f) down_0_1 = 0.0f;
            if (down_0_1 > 1.0f) down_0_1 = 1.0f;

            fluid_5x25_step(&g_fluid, left_0_1, down_0_1);

            // LED driver expects 5 segment bytes.
            fluid_5x25_to_segments(&g_fluid, LED_Buffer);

            // Keep the 6th byte unused/zero.
            LED_Buffer[5] = 0x00;
        } else {
            LED_Buff_setInt(led_value, LED_Buffer, 5);
        }
}

static void timer_cb(void)
{
    // static uint8_t accel_counter = 0; 

    if(LED_Display_state) {
        // on each timer callback, swap LED lines so that each line is written to
        // after each line has been written to, refresh the menu
            current_line++;
            current_line%=6;
            if(current_line == 0)
            {
                    counter_ms++;
                    if(counter_ms >=55)// every 495ms
                    {
                        counter_ms = 0;
                        counter_time++;
                        // no arch_printf() here: it allocates from the kernel heap,
                        // which is not safe from an interrupt
                    }
                    // The fluid simulation is too heavy for this 1.5 ms interrupt (it
                    // starved BLE and the watchdog): fluid_timer_cb() updates LED_Buffer
                    if (user_state != FLUID_MODE) {
                        memset(LED_Buffer,0x00,sizeof(LED_Buffer));
                        refreshMenu();
                    }
            }

            LED_write(LED_Buffer, current_line);
    }
}

void start_main_timer(void);

static void read_and_notify_accel(void) {
    accel_data_t data;
    if (!accel_cmd_readaccel(&data)) {
        // the I2C setup was lost (e.g. peripherals powered down by sleep):
        // re-initialize, the next tick will read again
        accel_init();
        accel_config();
        return;
    }
    // cache converted-to-mg values for display (avoid I2C from ISR)
    accel_data_t mg = data;
    accel_convert_to_mg(&mg, sens);
    g_last_accel_mg = mg;

    #if (BLE_CUSTOM1_SERVER)
        update_accel_data(&sens, &data); // Send real accelerometer data (raw)
        notify_accel_data(&sens, &data); // notify corresponding devices
    #endif
}

void user_accel_streaming(bool enable) {
    if (enable == accel_streaming) {
        return;
    }
    accel_streaming = enable;
    arch_printf("Accel streaming: %d\r\n", enable);

    if (enable) {
        // Keep the I2C peripheral powered while streaming (sleep turns it off)
        arch_force_active_mode();
        accel_init();
        accel_config();
        sens = SENS_16G;

        if (!has_timer_started) {
            has_timer_started = true;
            start_main_timer();
        }
    } else if (!LED_Display_state) {
        arch_restore_sleep_mode();
    }
}

// ---- PPG (heart-rate sensor) streaming ----
static bool ppg_streaming = false;
// main timer ticks (100 ms) without notification, so that the client can read the
// result of a register read from the characteristic value
static uint8_t ppg_hold = 0;

void user_ppg_streaming(bool enable) {
    if (enable == ppg_streaming || (enable && suota_active)) {
        return;
    }
    if (enable) {
        arch_force_active_mode(); // keep the I2C peripheral powered (as for the accel)
        accel_init();             // I2C controller set up
        ppg_streaming = ppg_start();
        arch_printf("PPG start: %d\r\n", ppg_streaming);
        if (!ppg_streaming) {
            arch_restore_sleep_mode();
            return;
        }
        if (!has_timer_started) {
            has_timer_started = true;
            start_main_timer();
        }
    } else {
        ppg_streaming = false;
        ppg_stop();
        arch_restore_sleep_mode();
        arch_printf("PPG stop\r\n");
    }
}

// Register access from the client, to experiment (writes to the PPG characteristic):
//   [reg, value]        write a register
//   [0xFE, reg, n]      read n registers (n <= 16): the characteristic value becomes
//                       [0xFE, reg, n, values...] and notifications pause for 1 s
//   [0xFC, enable]      automatic gain control on (1) / off (0)
//   [0xFB, led]         LED of the LED channel: 0x10 green, 0x20 IR, 0x40 red, 0 off
//   [0xFA, mode]        0 heart rate, 1 SpO2 (PPG_MODE_*; back to 0 at each start)
//   [0xF9, red (2 bytes), IR (2 bytes)]  SpO2 optical leak, little endian
//                       (ppg_set_leak(): the client keeps it, e.g. per ring name)
//   [0xF8]              measure the SpO2 optical leak (ring off the finger, in the dark)
//   [0xF7, red, IR]     SpO2 LED currents (0: default)
// These command bytes are also trim registers (0xF0-0xFB) of the sensor: they can't
// be written with [reg, value]. Needs the I2C controller (set up 5 s after boot or
// by a subscription).
void user_ppg_config(const uint8_t *value, uint16_t len) {
    if (suota_active || !has_timer_started) {
        return;
    }
    if (len == 2 && value[0] == 0xFC) {
        ppg_agc_enable(value[1]);
    } else if (len == 2 && value[0] == 0xFB) {
        ppg_set_led(value[1]);
    } else if (len == 2 && value[0] == 0xFA) {
        ppg_set_mode(value[1]);
    } else if (len == 5 && value[0] == 0xF9) {
        ppg_set_leak(value[1] | (value[2] << 8), value[3] | (value[4] << 8));
    } else if (len == 1 && value[0] == 0xF8) {
        ppg_measure_leak();
    } else if (len == 3 && value[0] == 0xF7) {
        ppg_set_spo2_currents(value[1], value[2]);
    } else if (len == 2) {
        hx3605_write_reg(value[0], value[1]);
    } else if (len == 3 && value[0] == 0xFE && value[2] <= 16) {
        uint8_t result[3 + 16];
        memcpy(result, value, 3);
        if (hx3605_read_regs(value[1], &result[3], value[2])) {
            set_ppg_value(result, 3 + value[2]);
            ppg_hold = 10;
        }
    }
}

// Motion level for the PPG packets: largest change of the acceleration between two
// main timer ticks (100 ms) over the last second, in 8 mg units
static uint8_t ppg_motion(void) {
    static accel_data_t prev;
    static uint8_t history[10], idx;
    accel_data_t a = g_last_accel_mg;
    int32_t d = (a.x > prev.x ? a.x - prev.x : prev.x - a.x)
              + (a.y > prev.y ? a.y - prev.y : prev.y - a.y)
              + (a.z > prev.z ? a.z - prev.z : prev.z - a.z);
    prev = a;
    history[idx] = d / 8 > 255 ? 255 : (uint8_t)(d / 8);
    idx = (idx + 1) % 10;
    uint8_t level = 0;
    for (int i = 0; i < 10; i++) {
        if (history[i] > level) {
            level = history[i];
        }
    }
    return level;
}

static void ppg_poll(void) {
    ppg_set_motion(ppg_motion());
    if (ppg_hold) {
        ppg_hold--;
        return;
    }
    uint8_t packet[PPG_PACKET_MAX];
    uint8_t len = ppg_read(packet);
    if (len) {
        notify_ppg_data(packet, len);
    }
}
// ---- end PPG streaming ----

static void main_timer_cb(void) {

    if (suota_active) {
        start_main_timer();
        return;
    }

    if (accel_streaming) {
        read_and_notify_accel();
    }

    if (ppg_streaming) {
        ppg_poll();
    }

    if (!user_run) {
        start_main_timer();
        return; 
    }

    if (user_state == DEFAULT) {
        // default nothing state
        LED_GPIO_mode(0);
        user_run = false; 
    }
     else if (user_state == ACCEL_INIT) {
        
        // LED_GPIO_mode(0); 
        
        // // Reinitialize and reconfigure accelerometer in case it was powered down during sleep
        // accel_init();
        // accel_config();
        LED_GPIO_mode(1);
        arch_force_active_mode();

        bool accel_init_out = accel_init(); 

        bool accel_cfg_out = accel_config(); 

        // TODO: fix this; in the initial fluid calibration, we set sensitivity to 
        // 16G but reinit so we interpret as 16G but ring is measuring in like 4G or some default value
        bool accel_set_sens_out = 1; 
        // bool accel_set_sens_out = accel_cmd_set_sensitivity(SENS_16G);
        
        // accel_cmd_get_sensitivity(&sens); 
        sens = SENS_16G;

        // if (accel_init_out) {
        //     led_value = 1;
        // } else {
        //     led_value = 11; 
        // } 
        led_value = (accel_init_out << 2) | (accel_cfg_out << 1) | accel_set_sens_out;
        
        // stop after running once
        user_run = false; 
    }
    else if (user_state == FLUID_MODE) {
        if (display_wanted()) {
            LED_GPIO_mode(1); // e.g. back on after an aborted firmware update
        }

        // Expose a compact debug view of the fluid state over the accel notification:
        // x = bitmask of y=0..7 for grid x=0
        // y = bitmask of y=0..7 for grid x=1
        // z = bitmask of y=0..7 for grid x=2
        // accel_data_t data;
        // data.x = 0;
        // data.y = 0;
        // data.z = 0;

        // for (int yy = 0; yy < 8; yy++) {
        //     if (fluid_5x25_get(&g_fluid, 0, yy) != 0) data.x |= (1u << yy);
        //     if (fluid_5x25_get(&g_fluid, 1, yy) != 0) data.y |= (1u << yy);
        //     if (fluid_5x25_get(&g_fluid, 2, yy) != 0) data.z |= (1u << yy);
        // }

        // #if (BLE_CUSTOM1_SERVER)
        //     update_accel_data(&sens, &data);
        //     notify_accel_data(&sens, &data);
        // #endif

        if (!accel_streaming) {
            read_and_notify_accel(); // still needed for the fluid display
        }
        // the fluid itself is stepped by fluid_timer_cb() (100 Hz)

        user_run = true;
    } else if (user_state == BATT_MODE) {
        LED_GPIO_mode(1); 
        
        adc_config_t cfg =
        {
            .mode = ADC_INPUT_MODE_SINGLE_ENDED,
            .sign = true,
            .attn = true
        };

        adc_init(&cfg);
        arch_asm_delay_us(20);
        adc_set_se_input(ADC_INPUT_SE_P0_1); // Read the dedicated ADC pin!
        uint32_t sample1 = adc_get_sample();
        arch_asm_delay_us(2);
        
        cfg.sign = false;
        adc_init(&cfg);
        adc_set_se_input(ADC_INPUT_SE_P0_1);
        uint32_t sample2 = adc_get_sample();
        
        uint32_t raw_adc = (sample1 + sample2);
        adc_disable();

        // led_value = (uint8_t)(raw_adc & 0xFF); 
        

        #if (BLE_CUSTOM1_SERVER)
            update_batt_data(raw_adc); // Send real accelerometer data (raw)
            notify_batt_data(raw_adc); // notify corresponding devices
        #endif
        user_run = true; 
    } else if (user_state == BT_INIT) {
        
        // // Skip accelerometer reads - just send test data
        // accel_data_t data; 
        // accel_cmd_readaccel(&data); 
        // accel_convert_to_mg(&data, sens); 

        // #if (BLE_CUSTOM1_SERVER)
        //     uint8_t out = update_accel_data(&data);
        //     led_value = out; 
        // #endif

        LED_GPIO_mode(0); 
        arch_force_active_mode();
        
        // Reinitialize and reconfigure accelerometer in case it was powered down during sleep
        accel_init();
        accel_config();

        
        user_run = false; 
        // user_run = true;
    } else if (user_state == BT_NOTIF) {
        // Force active mode to prevent sleep from powering down I2C peripheral
        
        if (!accel_streaming) {
            read_and_notify_accel();
        }
        
        user_run = true; // Continuous updates
    } else {
        // Restore sleep mode after operations complete
        arch_restore_sleep_mode();
        // stop running if invalid state
        user_run = false; 
    }

    start_main_timer();
}


void start_refresh_timer(void)
{
        static tim0_2_clk_div_config_t clk_div_config =
        {
            .clk_div  = TIM0_2_CLK_DIV_8
        };
        timer0_2_clk_enable();
        timer0_2_clk_div_set(&clk_div_config);
        timer0_set_pwm_high_counter(0);
        timer0_set_pwm_low_counter(0);
        // Set timer with 2MHz source clock divided by 10 so Fclk = 2MHz/10 = 200kHz
        timer0_init(TIM0_CLK_FAST, PWM_MODE_ONE, TIM0_CLK_DIV_BY_10);
        // reload value for 100ms (T = 1/200kHz * RELOAD_100MS = 0,000005 * 20000 = 100ms)
        timer0_set_pwm_on_counter(300);
        timer0_register_callback(timer_cb);
}

void start_main_timer(void) {
    // Timer interval in 10ms units (50 = 500ms, 10 = 100ms, 5 = 50ms)
    // Safe range: 5-10 (50-100ms) for 10-20Hz update rate
    main_timer_hnd = app_easy_timer(10, main_timer_cb); // 50ms = 20Hz
}

static void app_wakeup_cb(void)
{
    // If state is not idle, ignore the message
    if (ke_state_get(TASK_APP) == APP_CONNECTABLE)
    {
        default_advertise_operation();
    }
}

static void app_resume_system_from_sleep(void)
{
    if (GetBits16(SYS_STAT_REG, PER_IS_DOWN))
    {
        periph_init();
    }

    if (arch_ble_ext_wakeup_get())
    {
        arch_set_sleep_mode(app_default_sleep_mode);
        arch_ble_force_wakeup();
        arch_ble_ext_wakeup_off();
        app_easy_wakeup();
    }
}

// BLE device name, "LibRing_" + the ring ID (see ring_id())
char user_device_name[] = USER_DEVICE_NAME_PREFIX "00";

// BLE address read from the OTP by the SDK (arch_system.c), blank on these rings.
// When not null, the BLE stack uses it instead of the address in the image (nvds.c).
extern struct bd_addr dev_bdaddr;

// The default BLE address (CFG_NVDS_TAG_BD_ADDRESS), byte-reversed: suota_flash.py
// replaces the copies of it in the normal byte order with each ring's own address.
static const uint8_t default_bd_addr_reversed[NVDS_LEN_BD_ADDRESS] =
    {0x48, 0x23, 0x35, 0xF4, 0x00, 0x10}; // keep in sync with CFG_NVDS_TAG_BD_ADDRESS

// The BLE address the stack uses (in little endian): from the OTP, else from the image
static bool get_bd_address(uint8_t addr[NVDS_LEN_BD_ADDRESS])
{
    nvds_tag_len_t len = NVDS_LEN_BD_ADDRESS;
    return nvds_get_func(NVDS_TAG_BD_ADDRESS, &len, addr) == NVDS_OK
        && len == NVDS_LEN_BD_ADDRESS;
}

static bool is_default_bd_address(const uint8_t addr[NVDS_LEN_BD_ADDRESS])
{
    for (int i = 0; i < NVDS_LEN_BD_ADDRESS; i++) {
        if (addr[i] != default_bd_addr_reversed[NVDS_LEN_BD_ADDRESS - 1 - i]) {
            return false;
        }
    }
    return true;
}

#define FLASH_UID_LEN 16

// Unique ID of the SPI flash chip (Read Unique ID, 0x4B), programmed in the factory:
// the DA14585 itself has no unique ID. 16 bytes are read, not the 8 of
// spi_flash_read_unique_id(): the Puya flash of these rings has a 128-bit ID whose
// first 8 bytes are a lot code ("BP220533" + 0x15), the same for all the rings of a
// lot. Chips without a unique ID return a constant byte (00/FF).
static bool read_flash_unique_id(uint8_t uid[FLASH_UID_LEN])
{
    bool ok = false;
    spi_flash_release_from_power_down();
    arch_asm_delay_us(50); // wake-up time (tRES1)
    // same sequence as spi_flash_read_unique_id()
    if (spi_flash_is_busy() == SPI_FLASH_ERR_OK) {
        spi_set_bitmode(SPI_MODE_8BIT);
        spi_cs_low();
        spi_access(SPI_FLASH_OP_RUID);
        for (int i = 0; i < 4; i++) {
            spi_access(0x00); // dummy bytes
        }
        for (int i = 0; i < FLASH_UID_LEN; i++) {
            uid[i] = spi_access(0x00);
        }
        spi_cs_high();
        for (int i = 1; i < FLASH_UID_LEN; i++) {
            if (uid[i] != uid[0]) {
                ok = true;
            }
        }
    }
    spi_flash_power_down(); // as after a SUOTA update, which wakes it up again
    return ok;
}

// 24 bits from all the unique ID bytes (FNV-1a hash, folded)
static uint32_t flash_unique_id_hash(const uint8_t uid[FLASH_UID_LEN])
{
    uint32_t h = 0x811C9DC5;
    for (int i = 0; i < FLASH_UID_LEN; i++) {
        h = (h ^ uid[i]) * 0x01000193;
    }
    return (h ^ (h >> 24)) & 0xFFFFFF;
}

// Rings flashed with the same image share its default BLE address, which confuses
// the hosts (they merge them into one device). Unless the ring has its own address
// (from the OTP, or written in the image by suota_flash.py), replace the last 3 bytes
// of the default one (48:23:35:F4:00:10) with a hash of the flash unique ID: a
// stable address per ring, 48:23:35:XX:YY:ZZ. Without a flash unique ID, the default
// address is kept (only the name differs, see ring_id()).
// Called by periph_init(), at boot before the BLE stack reads the address, but also
// at each wake-up: only the first call does something.
void user_bd_address_init(void)
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

    uint8_t addr[NVDS_LEN_BD_ADDRESS];
    uint8_t uid[FLASH_UID_LEN];
    if (!get_bd_address(addr) || !is_default_bd_address(addr) || !read_flash_unique_id(uid)) {
        return;
    }
    uint32_t id = flash_unique_id_hash(uid);
    if (id == 0xF40010) {
        id ^= 1; // not the default address
    }
    addr[0] = id & 0xFF; // little endian: addr[0] is the last byte displayed
    addr[1] = (id >> 8) & 0xFF;
    addr[2] = (id >> 16) & 0xFF;
    memcpy(dev_bdaddr.addr, addr, NVDS_LEN_BD_ADDRESS);
}

// 8-bit ID of this ring, used in its name: the last byte of its BLE address (as
// displayed: AA:BB:CC:DD:EE:FF -> FF), unless it still has the default address
// (no address of its own and no flash unique ID, see user_bd_address_init()): then a
// random value (seeded by the TRNG at boot), which changes at each reboot.
// Only 256 values: 2 rings can have the same name, their addresses still differ.
static uint8_t ring_id(void)
{
    uint8_t addr[NVDS_LEN_BD_ADDRESS];
    if (get_bd_address(addr) && !is_default_bd_address(addr)) {
        return addr[0]; // little endian: addr[0] is the last byte displayed
    }
    return co_rand_byte();
}

static void user_device_name_init(void)
{
    static const char hex[] = "0123456789ABCDEF";
    uint8_t id = ring_id();
    g_ring_id = id; // shown by a long press
    char *digits = &user_device_name[sizeof(USER_DEVICE_NAME_PREFIX) - 1];
    digits[0] = hex[id >> 4];
    digits[1] = hex[id & 0xF];
    // app_init() already copied the name for the GAP Device Name characteristic
    memcpy(device_info.dev_name.name, user_device_name, USER_DEVICE_NAME_LEN);
    device_info.dev_name.length = USER_DEVICE_NAME_LEN;
    arch_printf("Device name: %s\r\n", user_device_name);
}

void user_app_on_init(void)
{

     default_app_on_init();
     user_device_name_init(); // before advertising, which puts the name in the adv. data
     //  start_main_timer();
     arch_printf("Booted now\r\n");
     LED_GPIO_mode(0);

     // Initialize fluid simulation once at boot
     fluid_5x25_init(&g_fluid);
}

// Fluid simulation step, every 10 ms (the shortest easy timer). The fluid speed
// is set by the step rate: it originally ran after each LED frame (6 lines x
// 1.5 ms = 9 ms, ~111 Hz) and at 10 Hz (main timer) it was ~11x too slow.
// Running it here (main loop, not an interrupt) keeps it from starving BLE.
static void fluid_timer_cb(void)
{
    if (!suota_active && user_state == FLUID_MODE && LED_Display_state
            && display_mode == DISPLAY_FLUID) {
        refreshMenu(); // fluid step + LED_Buffer update
    }
    fluid_timer_hnd = app_easy_timer(1, fluid_timer_cb); // 10 ms
}

static void fluid_start_cb(void)
{
    // Before the display is on: in other states, the timer0 interrupt overwrites
    // LED_Buffer (it may hold the ring ID after a long press during the first 5 s)
    user_state = FLUID_MODE;

    // The display (fluid simulation unless changed by a long press) also keeps
    // the system awake, so the accelerometer I2C bus stays powered
    display_started = true;
    if (display_wanted()) {
        LED_GPIO_mode(1);
    }
    accel_init();
    accel_config();
    sens = SENS_16G;

    user_run = true;
    // the main timer may already run if a client subscribed to accel during
    // the first 5 s (user_accel_streaming): don't start a second chain
    if (!has_timer_started) {
        has_timer_started = true;
        start_main_timer();
    }
    if (fluid_timer_hnd == EASY_TIMER_INVALID_TIMER) {
        fluid_timer_hnd = app_easy_timer(1, fluid_timer_cb);
    }
}

void user_app_on_db_init_complete(void)
{
    default_app_on_db_init_complete();

    // Start the display a few seconds after boot: if a firmware bug ever makes
    // the ring reset in a loop, this leaves a window to start a SUOTA update
    // (which pauses the display) and flash a fixed firmware
    app_easy_timer(500, fluid_start_cb); // 5 s
}

static void app_button_press_cb(void)
{
    app_resume_system_from_sleep();

    app_button_enable();

    bool pin_val = GPIO_GetPinStatus(GPIO_BUTTON_PORT, GPIO_BUTTON_PIN);
    #if (BLE_CUSTOM1_SERVER)
        // if (btn_send_enabled) {
        update_btn_data(pin_val); 
        notify_btn_data(pin_val);
        // }
    #endif 

    // if (!pin_val)
    // {
    //         if (button_hold_timer_hnd != EASY_TIMER_INVALID_TIMER)
    //         {
    //             app_easy_timer_cancel(button_hold_timer_hnd);
    //             button_hold_timer_hnd = EASY_TIMER_INVALID_TIMER;
    //         }
    //         return; // only set pressed when button goes from pressed to not pressed
    // }
    // arch_printf("Button was just pressed\r\n");

    // if (button_hold_timer_hnd != EASY_TIMER_INVALID_TIMER)
    // {
    //     app_easy_timer_cancel(button_hold_timer_hnd);
    // }
    // rising edge
    // Pressed: long press after 3 s (display mode), reset after 10 s.
    // Released: cancel whichever is pending. Taps do nothing.
    if (pin_val && button_hold_timer_hnd == EASY_TIMER_INVALID_TIMER) {
        button_hold_timer_hnd = app_easy_timer(LONG_PRESS_TIME, button_hold_timer_cb);
    } else if (!pin_val && button_hold_timer_hnd != EASY_TIMER_INVALID_TIMER) {
        app_easy_timer_cancel(button_hold_timer_hnd);
        button_hold_timer_hnd = EASY_TIMER_INVALID_TIMER;
    }
}

void app_button_enable(void)
{
    app_easy_wakeup_set(app_wakeup_cb);
    wkupct_register_callback(app_button_press_cb);

    if (!GPIO_GetPinStatus(GPIO_BUTTON_PORT, GPIO_BUTTON_PIN))
    {
        wkupct_enable_irq(WKUPCT_PIN_SELECT(GPIO_BUTTON_PORT, GPIO_BUTTON_PIN), // select pin (GPIO_BUTTON_PORT, GPIO_BUTTON_PIN)
                          WKUPCT_PIN_POLARITY(GPIO_BUTTON_PORT, GPIO_BUTTON_PIN, WKUPCT_PIN_POLARITY_HIGH), // polarity low
                          1, // 1 event
                          0); // debouncing time = 0
    }else{
            wkupct_enable_irq(WKUPCT_PIN_SELECT(GPIO_BUTTON_PORT, GPIO_BUTTON_PIN), // select pin (GPIO_BUTTON_PORT, GPIO_BUTTON_PIN)
                              WKUPCT_PIN_POLARITY(GPIO_BUTTON_PORT, GPIO_BUTTON_PIN, WKUPCT_PIN_POLARITY_LOW), // polarity low
                              1, // 1 event
                              0); // debouncing time = 0
    }
}

#if (BLE_SUOTA_RECEIVER)
void on_suotar_status_change(const uint8_t suotar_event)
{
    if (suotar_event == SUOTAR_START) {
        user_ppg_streaming(false); // before suota_active (ppg_stop() uses the I2C)
        suota_active = true;
        LED_GPIO_mode(0);
        arch_force_active_mode(); // stay awake during the update (keeps the I2C setup)
    } else if (suotar_event == SUOTAR_END) {
        suota_active = false; // display resumes if the update was aborted
    }

#if (!SUOTAR_SPI_DISABLE)
    uint8_t dev_id;

    // Release the SPI flash memory from power down
    spi_flash_release_from_power_down();

    // Try to auto-detect the SPI flash memory
    spi_flash_auto_detect(&dev_id);

    // Disable the SPI flash memory protection (unprotect all sectors)
    spi_flash_configure_memory_protection(SPI_FLASH_MEM_PROT_NONE);

    if (suotar_event == SUOTAR_END)
    {
        // Power down the SPI flash memory
        spi_flash_power_down();
    }
#endif
}
#endif
void user_app_on_disconnect(struct gapc_disconnect_ind const *param)
{
    arch_printf("BLE Disconnected\r\n");
    default_app_on_disconnect(NULL);

    user_accel_streaming(false);
    user_ppg_streaming(false);

    // if (main_timer_hnd != EASY_TIMER_INVALID_TIMER) {
    //     app_easy_timer_cancel(main_timer_hnd);
    //     main_timer_hnd = EASY_TIMER_INVALID_TIMER;
    // }

#if (BLE_BATT_SERVER)
    app_batt_poll_stop();
#endif

#if (BLE_SUOTA_RECEIVER)
    // Issue a platform reset when it is requested by the suotar procedure
    if (suota_state.reboot_requested)
    {
        // Reboot request will be served
        suota_state.reboot_requested = 0;

        // Platform reset
        platform_reset(RESET_AFTER_SUOTA_UPDATE);
    }
#endif

}

void user_app_on_connect(uint8_t conidx, struct gapc_connection_req_ind const *param)
{
    default_app_on_connection(conidx, param);
    arch_printf("BLE Connected\r\n");
}

void app_advertise_complete(const uint8_t status)
{
    if ((status == GAP_ERR_NO_ERROR) || (status == GAP_ERR_CANCELED))
    {

    }

    if (status == GAP_ERR_CANCELED)
    {
        arch_ble_ext_wakeup_on();
        app_button_enable();
    }
}

void user_catch_rest_hndl(ke_msg_id_t const msgid,
                          void const *param,
                          ke_task_id_t const dest_id,
                          ke_task_id_t const src_id)
{
    switch(msgid)
    {
        case GATTC_EVENT_REQ_IND:
        {
            // Confirm unhandled indication to avoid GATT timeout
            struct gattc_event_ind const *ind = (struct gattc_event_ind const *) param;
            struct gattc_event_cfm *cfm = KE_MSG_ALLOC(GATTC_EVENT_CFM, src_id, dest_id, gattc_event_cfm);
            cfm->handle = ind->handle;
            KE_MSG_SEND(cfm);
        } break;

        #if (BLE_CUSTOM1_SERVER)
        case CUSTS1_VAL_WRITE_IND:
        {
            struct custs1_val_write_ind const *ind = (struct custs1_val_write_ind const *)param;
            user_custs1_wr_ind_handler(msgid, ind, dest_id, src_id);
        } break;
        #endif

        default:
            break;
    }
}

/// @} APP
