/**
 ****************************************************************************************
 *
 * @file user_custs1_impl.c
 *
 * @brief Custom 1 server implementation
 *
 * Copyright (C) 2012-2021 Renesas Electronics Corporation and/or its affiliates
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 * OR OTHER DEALINGS IN THE SOFTWARE.
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @addtogroup USER_APP
 * @{
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "user_custs1_impl.h"
#include "user_custs1_def.h"
#include "user_proxr.h"
#include "custs1_task.h"
#include "app_task.h"
#include "ke_msg.h"
#include "accelerometer.h"
#include "attm_db.h"
#include "custs1.h"
#include "app.h"
#include "prf_utils.h"
#include <string.h>

/*
 * EXTERNAL VARIABLE DECLARATIONS
 ****************************************************************************************
 */
extern uint32_t led_value;
extern uint8_t user_state; 
extern bool has_timer_started;
extern bool user_run; 

/*
 * FUNCTION DEFINITIONS
 ****************************************************************************************
 */
#if (BLE_CUSTOM1_SERVER)

void user_custs1_wr_ind_handler(ke_msg_id_t const msgid,
                                     struct custs1_val_write_ind const *param,
                                     ke_task_id_t const dest_id,
                                     ke_task_id_t const src_id)
{
	if (param->handle == SVC1_IDX_CTRL_POINT_VAL)
	{
		if(param->value[0] != 0)
		{
			// Turn on the LED
			// LED_GPIO_mode(1);
			// led_value = 123;

		} else {
			// LED_GPIO_mode(0);

            if (user_state == DEFAULT && !has_timer_started) {
                has_timer_started = true; 
                start_main_timer(); 
            }
            
            user_state = BATT_MODE; 
            user_run = true;
		}
	}
}

uint8_t update_accel_data(const accel_sensitivity_t* sens, const accel_data_t *accel_data)
{
    // CRITICAL: Check if service is initialized first
    struct custs1_env_tag *custs1_env = PRF_ENV_GET(CUSTS1, custs1);
    if (custs1_env == NULL) {
        return 12; // Service not initialized
    }
    
    // CRITICAL: Check if task is in correct state (not busy)
    // If busy, skip to avoid message queue overflow
    ke_state_t state = ke_state_get(TASK_ID_CUSTS1);
    if (state == CUSTS1_BUSY) {
        return 11; // Task is busy processing another message, skip this update
    }
    
    // Prepare data buffer (6 bytes)
    uint8_t data_buffer[7];
    
    if (accel_data != NULL) {
        // Pack real accelerometer data (big-endian format)
        data_buffer[0] = accel_data->x >> 8;
        data_buffer[1] = accel_data->x & 0xFF;
        data_buffer[2] = accel_data->y >> 8;
        data_buffer[3] = accel_data->y & 0xFF;
        data_buffer[4] = accel_data->z >> 8;
        data_buffer[5] = accel_data->z & 0xFF;
    } else {
        // Send test data if no accelerometer data provided
        data_buffer[0] = 0x40;
        data_buffer[1] = 0x00;
        data_buffer[2] = 0x20;
        data_buffer[3] = 0x00;
        data_buffer[4] = 0xE0;
        data_buffer[5] = 0x00;
    }

    if (sens != NULL) {
        data_buffer[6] = *sens; 
    } else {
        data_buffer[6] = 0x07; 
    }
    
    // Use message-based approach
    struct custs1_val_set_req *req = KE_MSG_ALLOC_DYN(CUSTS1_VAL_SET_REQ,
                                                       prf_get_task_from_id(TASK_ID_CUSTS1),
                                                       TASK_APP,
                                                       custs1_val_set_req,
                                                       DEF_SVC1_ACCEL_CHAR_LEN);
    
    if (req == NULL) {
        return 10; // Allocation failed - heap might be full
    }
    
    // Set the message parameters
    req->conidx = 0;
    req->handle = SVC1_IDX_ACCEL_VAL;
    req->length = DEF_SVC1_ACCEL_CHAR_LEN;
    
    // Copy data to message
    memcpy(req->value, data_buffer, DEF_SVC1_ACCEL_CHAR_LEN);
    
    // Send the message
    ke_msg_send(req);

    return 121; 
}

uint8_t notify_accel_data(const accel_sensitivity_t* sens, const accel_data_t *accel_data) {

    struct custs1_env_tag *custs1_env = PRF_ENV_GET(CUSTS1, custs1);
    if (custs1_env == NULL) {
        return 12; // Service not initialized
    }
    
    // CRITICAL: Check if task is in correct state (not busy)
    // If busy, skip to avoid message queue overflow
    ke_state_t state = ke_state_get(TASK_ID_CUSTS1);
    if (state == CUSTS1_BUSY) {
        return 11; // Task is busy processing another message, skip this update
    }


    // Prepare data buffer (6 bytes)
    uint8_t data_buffer[7];
    
    if (accel_data != NULL) {
        // Pack real accelerometer data (big-endian format)
        data_buffer[0] = accel_data->x >> 8;
        data_buffer[1] = accel_data->x & 0xFF;
        data_buffer[2] = accel_data->y >> 8;
        data_buffer[3] = accel_data->y & 0xFF;
        data_buffer[4] = accel_data->z >> 8;
        data_buffer[5] = accel_data->z & 0xFF;
    } else {
        // Send test data if no accelerometer data provided
        data_buffer[0] = 0x40;
        data_buffer[1] = 0x00;
        data_buffer[2] = 0x20;
        data_buffer[3] = 0x00;
        data_buffer[4] = 0xE0;
        data_buffer[5] = 0x00;
    }

    if (sens != NULL) {
        data_buffer[6] = *sens; 
    } else {
        data_buffer[6] = 0x07; 
    }

    // Use message-based approach
    struct custs1_val_ntf_ind_req *req = KE_MSG_ALLOC_DYN(CUSTS1_VAL_NTF_REQ,
                                                            prf_get_task_from_id(TASK_ID_CUSTS1),
                                                            TASK_APP,
                                                            custs1_val_ntf_ind_req,
                                                            DEF_SVC1_ACCEL_CHAR_LEN);

    if (req == NULL) {
        return 10; // Allocation failed - heap might be full
    }

    req->conidx = 0; // Connection index (0 for first connection)
    req->handle = SVC1_IDX_ACCEL_VAL;
    req->length = DEF_SVC1_ACCEL_CHAR_LEN;
    req->notification = true;

    memcpy(req->value, data_buffer, DEF_SVC1_ACCEL_CHAR_LEN);

    ke_msg_send(req); 

    return 171; 
}

void update_btn_data(uint8_t btn_pressed) {
    
    // CRITICAL: Check if service is initialized first
    struct custs1_env_tag *custs1_env = PRF_ENV_GET(CUSTS1, custs1);
    if (custs1_env == NULL) {
        return; 
    }
    
    // CRITICAL: Check if task is in correct state (not busy)
    // If busy, skip to avoid message queue overflow
    ke_state_t state = ke_state_get(TASK_ID_CUSTS1);
    if (state == CUSTS1_BUSY) {
        return; 
    }

    uint8_t data_buf[1] = {btn_pressed}; 
    
    // Use message-based approach
    struct custs1_val_set_req *req = KE_MSG_ALLOC_DYN(CUSTS1_VAL_SET_REQ,
                                                       prf_get_task_from_id(TASK_ID_CUSTS1),
                                                       TASK_APP,
                                                       custs1_val_set_req,
                                                       DEF_SVC1_BTN_CHAR_LEN);
    
    if (req == NULL) {
        return;
    }
    
    // Set the message parameters
    req->conidx = 0;
    req->handle = SVC1_IDX_BTN_VAL;
    req->length = DEF_SVC1_BTN_CHAR_LEN;
    
    // Copy data to message
    memcpy(req->value, data_buf, DEF_SVC1_BTN_CHAR_LEN);
    
    // Send the message
    ke_msg_send(req);
}

void notify_btn_data(uint8_t btn_pressed) {

    struct custs1_env_tag *custs1_env = PRF_ENV_GET(CUSTS1, custs1);
    if (custs1_env == NULL) {
        return; // Service not initialized
    }
    
    // CRITICAL: Check if task is in correct state (not busy)
    // If busy, skip to avoid message queue overflow
    ke_state_t state = ke_state_get(TASK_ID_CUSTS1);
    if (state == CUSTS1_BUSY) {
        return; // Task is busy processing another message, skip this update
    }


    // Prepare data buffer (6 bytes)
    uint8_t data_buffer[1] = {btn_pressed};

    // Use message-based approach
    struct custs1_val_ntf_ind_req *req = KE_MSG_ALLOC_DYN(CUSTS1_VAL_NTF_REQ,
                                                            prf_get_task_from_id(TASK_ID_CUSTS1),
                                                            TASK_APP,
                                                            custs1_val_ntf_ind_req,
                                                            DEF_SVC1_BTN_CHAR_LEN);

    if (req == NULL) {
        return; // Allocation failed - heap might be full
    }

    req->conidx = 0; // Connection index (0 for first connection)
    req->handle = SVC1_IDX_BTN_VAL;
    req->length = DEF_SVC1_BTN_CHAR_LEN;
    req->notification = true;

    memcpy(req->value, data_buffer, DEF_SVC1_BTN_CHAR_LEN);

    ke_msg_send(req); 
}



void update_batt_data(uint32_t batt) {

    
    // CRITICAL: Check if service is initialized first
    struct custs1_env_tag *custs1_env = PRF_ENV_GET(CUSTS1, custs1);
    if (custs1_env == NULL) {
        return; 
    }
    
    // CRITICAL: Check if task is in correct state (not busy)
    // If busy, skip to avoid message queue overflow
    ke_state_t state = ke_state_get(TASK_ID_CUSTS1);
    if (state == CUSTS1_BUSY) {
        return; 
    }

    uint8_t data_buf[4] = {(batt >> 24) & 0xFF, (batt >> 16) & 0xFF, (batt >> 8) & 0xFF, (batt >> 0) & 0xFF}; 
    
    // Use message-based approach
    struct custs1_val_set_req *req = KE_MSG_ALLOC_DYN(CUSTS1_VAL_SET_REQ,
                                                       prf_get_task_from_id(TASK_ID_CUSTS1),
                                                       TASK_APP,
                                                       custs1_val_set_req,
                                                       DEF_SVC1_BATT_CHAR_LEN);
    
    if (req == NULL) {
        return;
    }
    
    // Set the message parameters
    req->conidx = 0;
    req->handle = SVC1_IDX_BATT_VAL;
    req->length = DEF_SVC1_BATT_CHAR_LEN;
    
    // Copy data to message
    memcpy(req->value, data_buf, DEF_SVC1_BATT_CHAR_LEN);
    
    // Send the message
    ke_msg_send(req);
}

void notify_batt_data(uint32_t batt) {


    struct custs1_env_tag *custs1_env = PRF_ENV_GET(CUSTS1, custs1);
    if (custs1_env == NULL) {
        return; // Service not initialized
    }
    
    // CRITICAL: Check if task is in correct state (not busy)
    // If busy, skip to avoid message queue overflow
    ke_state_t state = ke_state_get(TASK_ID_CUSTS1);
    if (state == CUSTS1_BUSY) {
        return; // Task is busy processing another message, skip this update
    }


    // Prepare data buffer (4 bytes)
    uint8_t data_buf[4] = {(batt >> 24) & 0xFF, (batt >> 16) & 0xFF, (batt >> 8) & 0xFF, (batt >> 0) & 0xFF}; 

    // Use message-based approach
    struct custs1_val_ntf_ind_req *req = KE_MSG_ALLOC_DYN(CUSTS1_VAL_NTF_REQ,
                                                            prf_get_task_from_id(TASK_ID_CUSTS1),
                                                            TASK_APP,
                                                            custs1_val_ntf_ind_req,
                                                            DEF_SVC1_BATT_CHAR_LEN);

    if (req == NULL) {
        return; // Allocation failed - heap might be full
    }

    req->conidx = 0; // Connection index (0 for first connection)
    req->handle = SVC1_IDX_BATT_VAL;
    req->length = DEF_SVC1_BATT_CHAR_LEN;
    req->notification = true;

    memcpy(req->value, data_buf, DEF_SVC1_BATT_CHAR_LEN);

    ke_msg_send(req);
}


#endif //BLE_CUSTOM1_SERVER