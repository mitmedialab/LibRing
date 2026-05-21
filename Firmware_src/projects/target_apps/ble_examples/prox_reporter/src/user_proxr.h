/**
 ****************************************************************************************
 *
 * @file user_proxr.h
 *
 * @brief Proximity Project application header file.
 *
 * Copyright (C) 2015-2024 Renesas Electronics Corporation and/or its affiliates.
 * All rights reserved. Confidential Information.
 *
 * This software ("Software") is supplied by Renesas Electronics Corporation and/or its
 * affiliates ("Renesas"). Renesas grants you a personal, non-exclusive, non-transferable,
 * revocable, non-sub-licensable right and license to use the Software, solely if used in
 * or together with Renesas products. You may make copies of this Software, provided this
 * copyright notice and disclaimer ("Notice") is included in all such copies. Renesas
 * reserves the right to change or discontinue the Software at any time without notice.
 *
 * THE SOFTWARE IS PROVIDED "AS IS". RENESAS DISCLAIMS ALL WARRANTIES OF ANY KIND,
 * WHETHER EXPRESS, IMPLIED, OR STATUTORY, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. TO THE
 * MAXIMUM EXTENT PERMITTED UNDER LAW, IN NO EVENT SHALL RENESAS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE, EVEN IF RENESAS HAS BEEN ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES. USE OF THIS SOFTWARE MAY BE SUBJECT TO TERMS AND CONDITIONS CONTAINED IN
 * AN ADDITIONAL AGREEMENT BETWEEN YOU AND RENESAS. IN CASE OF CONFLICT BETWEEN THE TERMS
 * OF THIS NOTICE AND ANY SUCH ADDITIONAL LICENSE AGREEMENT, THE TERMS OF THE AGREEMENT
 * SHALL TAKE PRECEDENCE. BY CONTINUING TO USE THIS SOFTWARE, YOU AGREE TO THE TERMS OF
 * THIS NOTICE.IF YOU DO NOT AGREE TO THESE TERMS, YOU ARE NOT PERMITTED TO USE THIS
 * SOFTWARE.
 *
 ****************************************************************************************
 */

#ifndef _USER_PROXR_H_
#define _USER_PROXR_H_

/**
 ****************************************************************************************
 * @addtogroup APP
 * @ingroup RICOW
 *
 * @brief User Application entry point.
 *
 * @{
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */

#include <stdint.h>
#include "gapc_task.h"

/*
 * DEFINES
 ****************************************************************************************
 */

/****************************************************************************************
* Selects the sleep mode that the device will enter after advertising completion        *
*  - CFG_APP_GOTO_DEEP_SLEEP        Deep Sleep mode                                     *
*  - CFG_APP_GOTO_HIBERNATION       Hibernation mode (DA14531 only)                     *
*  - CFG_APP_GOTO_STATEFUL_HIBERNATION   Stateful Hibernation mode (DA14531 only)       *
* Note: If none is defined, then system will enter the selected Extended sleep mode     *
*                                                                                       *
* IMPORTANT: If CFG_APP_GOTO_STATEFUL_HIBERNATION is defined, CFG_STATEFUL_HIBERNATION  *
* shall be defined as well (in Options for Target 'DA14531' --> Asm -->                 *
* Conditional Assembly Control Symbols --> Define:)                                     *
****************************************************************************************/
#undef CFG_APP_GOTO_DEEP_SLEEP

#if defined (__DA14531__)
#undef CFG_APP_GOTO_HIBERNATION
#undef CFG_APP_GOTO_STATEFUL_HIBERNATION

#if defined (CFG_APP_GOTO_HIBERNATION)
/****************************************************************************************
 * Hibernation mode configuration                                                       *
 ****************************************************************************************/
#define CFG_HIBERNATION_RAM1            PD_SYS_DOWN_RAM_OFF
#define CFG_HIBERNATION_RAM2            PD_SYS_DOWN_RAM_OFF
#define CFG_HIBERNATION_RAM3            PD_SYS_DOWN_RAM_OFF
#define CFG_HIBERNATION_REMAP           REMAP_ADDR0_TO_ROM
#define CFG_HIBERNATION_PAD_LATCH_EN    false
#elif defined (CFG_APP_GOTO_STATEFUL_HIBERNATION)
/****************************************************************************************
 * Stateful Hibernation mode configuration                                              *
 ****************************************************************************************/
#define CFG_STATEFUL_HIBERNATION_RAM1           PD_SYS_DOWN_RAM_ON
#define CFG_STATEFUL_HIBERNATION_RAM2           PD_SYS_DOWN_RAM_ON
#define CFG_STATEFUL_HIBERNATION_RAM3           PD_SYS_DOWN_RAM_ON
#define CFG_STATEFUL_HIBERNATION_REMAP          STATEFUL_HIBERN_REMAP_ADDR0_TO_RAM1
#define CFG_STATEFUL_HIBERNATION_PAD_LATCH_EN   false
#endif // CFG_APP_GOTO_HIBERNATION
#endif // __DA14531__

#if defined (CFG_APP_GOTO_DEEP_SLEEP) && (defined (CFG_APP_GOTO_HIBERNATION) || defined (CFG_APP_GOTO_STATEFUL_HIBERNATION))
    #error "Config error: Can not define both CFG_APP_GOTO_DEEP_SLEEP and CFG_APP_GOTO_HIBERNATION/CFG_APP_GOTO_STATEFUL_HIBERNATION."
#endif

#if defined (CFG_SPI_FLASH_ENABLE) && (defined (CFG_APP_GOTO_HIBERNATION) || defined (CFG_APP_GOTO_STATEFUL_HIBERNATION))
    #error "Config error: Can not define both CFG_SPI_FLASH_ENABLE and CFG_APP_GOTO_HIBERNATION/CFG_APP_GOTO_STATEFUL_HIBERNATION."
#endif

#if defined (CFG_APP_GOTO_DEEP_SLEEP)
/****************************************************************************************
* Deep Sleep mode and Wake-up from Deep Sleep Configuration                             *
* Selects the trigger mechanism to wake up from deep sleep.                             *
*                                                                                       *
* - CFG_DEEP_SLEEP_WAKEUP_POR       - Wake up from POR pin                              *
*                                                                                       *
* - CFG_DEEP_SLEEP_WAKEUP_GPIO      - Wake up from WakeUp Controller                    *
*                                                                                       *
* - CFG_DEEP_SLEEP_WAKEUP_RTC       - Wake up from RTC Timer (only in DA1453x)          *
*                                                                                       *
* - CFG_DEEP_SLEEP_WAKEUP_TIMER1    - Wake up from Timer1 (only in DA1453x)             *
*                                                                                       *
* NOTE:                                                                                 *
* A hardware reset or power cycle will always wake up the system from deep sleep.       *
****************************************************************************************/
#undef CFG_DEEP_SLEEP_WAKEUP_POR
#define CFG_DEEP_SLEEP_WAKEUP_GPIO

/****************************************************************************************
 * Deep sleep mode configuration                                                        *
 ****************************************************************************************/
#define CFG_DEEP_SLEEP_RAM1            PD_SYS_DOWN_RAM_OFF
#define CFG_DEEP_SLEEP_RAM2            PD_SYS_DOWN_RAM_OFF
#define CFG_DEEP_SLEEP_RAM3            PD_SYS_DOWN_RAM_OFF
#define CFG_DEEP_SLEEP_PAD_LATCH_EN    false

#if defined (__DA14531__)
#undef CFG_DEEP_SLEEP_WAKEUP_RTC
#undef CFG_DEEP_SLEEP_WAKEUP_TIMER1
#endif

#if defined (CFG_DEEP_SLEEP_WAKEUP_POR) && defined (CFG_DEEP_SLEEP_WAKEUP_GPIO)
    #error "Config error: Can not define both CFG_DEEP_SLEEP_WAKEUP_POR and CFG_DEEP_SLEEP_WAKEUP_GPIO."
#endif

#if !defined (CFG_DEEP_SLEEP_WAKEUP_POR) && !defined (CFG_DEEP_SLEEP_WAKEUP_GPIO) && \
    !defined (CFG_DEEP_SLEEP_WAKEUP_RTC) && !defined (CFG_DEEP_SLEEP_WAKEUP_TIMER1)
    #error "Config error: At least one wake-up source has to be selected."
#endif

#endif

/****************************************************************************************
* Wake-up from Extended Sleep Configuration                                             *
* Selects the trigger mechanism to wake up from extended sleep.                         *
*                                                                                       *
* - CFG_EXT_SLEEP_WAKEUP_RTC       - Wake up from RTC Timer (only in DA1453x)           *
*                                                                                       *
* - CFG_EXT_SLEEP_WAKEUP_TIMER1    - Wake up from Timer1 (only in DA1453x)              *
*                                                                                       *
* NOTE:                                                                                 *
* If none is defined, then system will wake up from extended sleep by button press.     *
****************************************************************************************/
#undef CFG_EXT_SLEEP_WAKEUP_RTC
#undef CFG_EXT_SLEEP_WAKEUP_TIMER1

#if defined (CFG_EXT_SLEEP_WAKEUP_RTC) && defined (CFG_EXT_SLEEP_WAKEUP_TIMER1)
    #error "Config error: Can not define both CFG_EXT_SLEEP_WAKEUP_RTC and CFG_EXT_SLEEP_WAKEUP_TIMER1."
#endif

/*
 * FUNCTION DECLARATIONS
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @brief Enable push button. Register callback function for button press event.
 *        Must be called in periph_init().
 ****************************************************************************************
 */
void app_button_enable(void);

/**
 ****************************************************************************************
 * @brief Function to be called on the advertising completion event.
 * @param[in] status GAP Error code
 ****************************************************************************************
 */
void user_app_on_init(void);
void app_advertise_complete(const uint8_t status);

#if (BLE_SUOTA_RECEIVER)
/**
 ****************************************************************************************
 * @brief Function called when the SUOTAR status changes.
 * @param[in] suotar_event SUOTAR_START or SUOTAR_STOP
 ****************************************************************************************
 */
void on_suotar_status_change(const uint8_t suotar_event);
#endif // BLE_SUOTA_RECEIVER

/**
 ****************************************************************************************
 * @brief Disconnection function.
 * @param[in] param         Pointer to GAPC_DISCONNECT_IND message
 ****************************************************************************************
*/
void user_app_on_disconnect(struct gapc_disconnect_ind const *param);

void user_app_on_connect(uint8_t conidx, struct gapc_connection_req_ind const *param);
/**
 ****************************************************************************************
 * @brief Handles the messages that are not handled by the SDK internal mechanisms.
 * @param[in] msgid   Id of the message received.
 * @param[in] param   Pointer to the parameters of the message.
 * @param[in] dest_id ID of the receiving task instance.
 * @param[in] src_id  ID of the sending task instance.
 ****************************************************************************************
*/
void user_catch_rest_hndl(ke_msg_id_t const msgid,
                          void const *param,
                          ke_task_id_t const dest_id,
                          ke_task_id_t const src_id);

void LED_GPIO_mode(uint8_t mode); 

void start_main_timer(void);

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

/// @} APP

#endif // _USER_PROXR_H_
