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

#include "battery.h"

#include "adc.h"


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


uint8_t user_state = 0;
bool user_run = false;  

bool has_timer_started = false; 

static void button_hold_timer_cb(void)
{
    button_hold_timer_hnd = EASY_TIMER_INVALID_TIMER;

    // if (!GPIO_GetPinStatus(GPIO_BUTTON_PORT, GPIO_BUTTON_PIN))
    // {
    //     arch_printf("Button held for 5s, restarting\r\n");


        // #if (BLE_CUSTOM1_SERVER)
        //     // if (btn_send_enabled) {
        //     update_btn_data(5); 
        //     notify_btn_data(5);
        //     // }
        // #endif 
        platform_reset(RESET_NO_ERROR);
        // arch_force_active_mode();
        // NVIC_SystemReset();
    // }
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
            LED_Buffer[0] = 0b00000000; // 0b01111000; 
            LED_Buffer[1] = 0b00000000; // 0b01110000; 
            LED_Buffer[2] = 0; // 0b01111000; 
            LED_Buffer[3] = 0; // 0b01110000; 
            LED_Buffer[4] = 0; // 0b01111000; 
            LED_Buffer[5] = 0x00; 
            // LED_Buff_setInt(led_value, LED_Buffer, 5);
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
                        arch_printf("Time %i MS: %i\r\n", realUnix, lld_evt_time_get());
                        //if(realUnix - turnOnTime >= 10)
                            //       LED_GPIO_mode(0);
                    }
                    memset(LED_Buffer,0x00,sizeof(LED_Buffer));
                    refreshMenu();
            }

            LED_write(LED_Buffer, current_line);
    }
}

static void main_timer_cb(void) {

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
        LED_GPIO_mode(1);

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

        accel_data_t data;
        accel_cmd_readaccel(&data);
        // cache converted-to-mg values for display (avoid I2C from ISR)
        {
            accel_data_t mg = data;
            accel_convert_to_mg(&mg, sens);
            g_last_accel_mg = mg;
        }

        #if (BLE_CUSTOM1_SERVER)
            update_accel_data(&sens, &data); // Send real accelerometer data (raw)
            notify_accel_data(&sens, &data); // notify corresponding devices
        #endif

        user_run = true;
    } else if (user_state == BATT_MODE) {
        LED_GPIO_mode(1); 
        led_value = 88888;
        
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
        
        // Read accelerometer data
        accel_data_t data;
        accel_cmd_readaccel(&data);
        // cache converted-to-mg values for display (avoid I2C from ISR)
        {
            accel_data_t mg = data;
            accel_convert_to_mg(&mg, sens);
            g_last_accel_mg = mg;
        }

        #if (BLE_CUSTOM1_SERVER)
            update_accel_data(&sens, &data); // Send real accelerometer data (raw)
            notify_accel_data(&sens, &data); // notify corresponding devices
        #endif
        
        
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

void user_app_on_init(void)
{

     default_app_on_init();
     //  start_main_timer();
     arch_printf("Booted now\r\n");
     LED_GPIO_mode(0);

     // Initialize fluid simulation once at boot
     fluid_5x25_init(&g_fluid);
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
    if (pin_val && button_hold_timer_hnd == EASY_TIMER_INVALID_TIMER) {
        button_hold_timer_hnd = app_easy_timer(500, button_hold_timer_cb); // 5 seconds
    } else if (!pin_val && button_hold_timer_hnd != EASY_TIMER_INVALID_TIMER) {
        app_easy_timer_cancel(button_hold_timer_hnd);
        button_hold_timer_hnd = EASY_TIMER_INVALID_TIMER;
    }
    if (!pin_val) return; // only change state on rising edge

    if (user_state == DEFAULT && !has_timer_started) {
        has_timer_started = true; 
        start_main_timer(); 
    }

    if (user_state != BATT_MODE) {
        user_state = (user_state + 1) % NUM_STATES;
        user_run = true; 
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
