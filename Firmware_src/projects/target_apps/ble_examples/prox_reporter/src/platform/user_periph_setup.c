
#include "rwip_config.h"             // SW configuration
#include "datasheet.h"
#include "user_periph_setup.h"       // peripheral configuration
#include "system_library.h"
#include "user_profiles_config.h"
#include "gpio.h"
#include "uart.h"                    // UART initialization
#include "app_proxr.h"
#include "user_proxr.h"
#include "syscntl.h"
#include "fpga_helper.h"

#if BLE_BATT_SERVER
#include "app_bass.h"
#endif

#include "arch_console.h"
/*
 * GLOBAL VARIABLE DEFINITIONS
 ****************************************************************************************
 */

#if defined (CFG_SPI_FLASH_ENABLE)
#include "spi.h"
#include "spi_flash.h"
#endif

/*
 * FUNCTION DEFINITIONS
 ****************************************************************************************
 */

#if DEVELOPMENT_DEBUG

void GPIO_reservations(void)
{
/*
    i.e. to reserve P0_1 as Generic Purpose I/O:
*/
    RESERVE_GPIO(BATTERY_ADC, GPIO_PORT_0, GPIO_PIN_1, PID_ADC);
    
    // Push Button
    RESERVE_GPIO(SPI_DI, GPIO_LEDC_PORT, GPIO_LEDC_PIN, PID_GPIO);
    RESERVE_GPIO(PUSH_BUTTON, GPIO_BUTTON_PORT, GPIO_BUTTON_PIN, PID_GPIO);

#if defined (__DA14531__) && (defined (CFG_APP_GOTO_HIBERNATION) || defined (CFG_APP_GOTO_STATEFUL_HIBERNATION))
    // Wake up from hibernation pin
    RESERVE_GPIO(HIB_WAKE_UP, HIB_WAKE_UP_PORT, HIB_WAKE_UP_PIN, PID_GPIO);
#endif

#if BLE_PROX_REPORTER
    // Alert LED
    RESERVE_GPIO(GREEN_LED, GPIO_ALERT_LED_PORT, GPIO_ALERT_LED_PIN, PID_GPIO);
#endif

#if (BLE_BATT_SERVER && USE_BAT_LEVEL_ALERT)
    // Battery alert LED
    RESERVE_GPIO(RED_LED, GPIO_BAT_LED_PORT, GPIO_BAT_LED_PIN, PID_GPIO);
#endif

#if defined (CFG_PRINTF_UART2)
    // UART2 Tx pin
    RESERVE_GPIO(UART2_TX, UART2_TX_PORT, UART2_TX_PIN, PID_UART2_TX);
#endif

#if defined (CFG_SPI_FLASH_ENABLE) && !defined (__DA14586__)
    // SPI Flash
    RESERVE_GPIO(SPI_EN, SPI_EN_PORT, SPI_EN_PIN, PID_SPI_EN);
    RESERVE_GPIO(SPI_CLK, SPI_CLK_PORT, SPI_CLK_PIN, PID_SPI_CLK);
    RESERVE_GPIO(SPI_DO, SPI_DO_PORT, SPI_DO_PIN, PID_SPI_DO);
    RESERVE_GPIO(SPI_DI, SPI_DI_PORT, SPI_DI_PIN, PID_SPI_DI);
#endif
}
#endif //DEVELOPMENT_DEBUG


void set_pad_functions(void)
{
    GPIO_ConfigurePin(GPIO_LEDC_PORT, GPIO_LEDC_PIN, OUTPUT, PID_GPIO, false);
    GPIO_ConfigurePin(GPIO_BUTTON_PORT, GPIO_BUTTON_PIN, INPUT_PULLDOWN, PID_GPIO, false);

    // Configure P0_1 directly as Analog Input so ADC reads accurately
    GPIO_ConfigurePin(GPIO_PORT_0, GPIO_PIN_1, INPUT, PID_ADC, false);

#if (BLE_PROX_REPORTER)
    // Alert LED
    GPIO_ConfigurePin(GPIO_ALERT_LED_PORT, GPIO_ALERT_LED_PIN, OUTPUT, PID_GPIO, false);
#endif

#if (BLE_BATT_SERVER && USE_BAT_LEVEL_ALERT)
    // Battery alert LED
    GPIO_ConfigurePin(GPIO_BAT_LED_PORT, GPIO_BAT_LED_PIN, OUTPUT, PID_GPIO, false);
#endif

#if defined (CFG_PRINTF_UART2)
    // UART2 Tx pin
    GPIO_ConfigurePin(UART2_TX_PORT, UART2_TX_PIN, OUTPUT, PID_UART2_TX, false);
#endif

#if defined (CFG_SPI_FLASH_ENABLE)
    // SPI Flash
    GPIO_ConfigurePin(SPI_EN_PORT, SPI_EN_PIN, OUTPUT, PID_SPI_EN, true);
    GPIO_ConfigurePin(SPI_CLK_PORT, SPI_CLK_PIN, OUTPUT, PID_SPI_CLK, false);
    GPIO_ConfigurePin(SPI_DO_PORT, SPI_DO_PIN, OUTPUT, PID_SPI_DO, false);
    GPIO_ConfigurePin(SPI_DI_PORT, SPI_DI_PIN, INPUT, PID_SPI_DI, false);
#endif
}

#if defined (CFG_PRINTF_UART2)
// Configuration struct for UART2
static const uart_cfg_t uart_cfg = {
    .baud_rate = UART2_BAUDRATE,
    .data_bits = UART2_DATABITS,
    .parity = UART2_PARITY,
    .stop_bits = UART2_STOPBITS,
    .auto_flow_control = UART2_AFCE,
    .use_fifo = UART2_FIFO,
    .tx_fifo_tr_lvl = UART2_TX_FIFO_LEVEL,
    .rx_fifo_tr_lvl = UART2_RX_FIFO_LEVEL,
    .intr_priority = 2,
};
#endif

#if defined (CFG_SPI_FLASH_ENABLE)
// Configuration struct for SPI
static const spi_cfg_t spi_cfg = {
    .spi_ms = SPI_MS_MODE,
    .spi_cp = SPI_CP_MODE,
    .spi_speed = SPI_SPEED_MODE,
    .spi_wsz = SPI_WSZ,
    .spi_cs = SPI_CS,
    .cs_pad.port = SPI_EN_PORT,
    .cs_pad.pin = SPI_EN_PIN,
};

// Configuration struct for SPI FLASH
static const spi_flash_cfg_t spi_flash_cfg = {
    .chip_size = SPI_FLASH_DEV_SIZE,
};
#endif


void periph_init(void)
{
    // Power up peripherals' power domain
    SetBits16(PMU_CTRL_REG, PERIPH_SLEEP, 0);
    while (!(GetWord16(SYS_STAT_REG) & PER_IS_UP));
    SetBits16(CLK_16M_REG, XTAL16_BIAS_SH_ENABLE, 1);
    // ROM patch
    patch_func();

    // Initialize peripherals
#if defined (CFG_PRINTF_UART2)
    // Initialize UART2
    uart_initialize(UART2, &uart_cfg);
#endif

#if defined (CFG_SPI_FLASH_ENABLE)
    // Configure SPI Flash environment
    spi_flash_configure_env(&spi_flash_cfg);

    // Initialize SPI
    spi_initialize(&spi_cfg);
#endif

    // Set pad functionality
    set_pad_functions();

#if (BLE_PROX_REPORTER)
    app_proxr_port_reinit(GPIO_ALERT_LED_PORT, GPIO_ALERT_LED_PIN);
    app_button_enable();
#endif

#if BLE_BATT_SERVER
    app_batt_port_reinit();
#endif

    // Enable the pads
    GPIO_set_pad_latch_en(true);

    app_button_enable();
    start_refresh_timer();
}
