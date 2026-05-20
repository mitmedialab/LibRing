/**
 ****************************************************************************************
 *
 * @file user_custs1_impl.h
 *
 * @brief Custom 1 server implementation header file.
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

#ifndef _USER_CUSTS1_IMPL_H_
#define _USER_CUSTS1_IMPL_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "custs1_task.h"
#include "accelerometer.h"

#if (BLE_CUSTOM1_SERVER)
/*
 * FUNCTION DECLARATIONS
 ****************************************************************************************
 */

void user_custs1_wr_ind_handler(ke_msg_id_t const msgid,
                                     struct custs1_val_write_ind const *param,
                                     ke_task_id_t const dest_id,
                                     ke_task_id_t const src_id);

/**
 ****************************************************************************************
 * @brief Update the accelerometer characteristic value in the database
 * @param[in] accel_data Pointer to accelerometer data structure (can be NULL for test data)
 ****************************************************************************************
 */
uint8_t update_accel_data(const accel_sensitivity_t* sens, const accel_data_t *accel_data);

/**
 ****************************************************************************************
 * @brief Send accelerometer data notification to connected client
 * @param[in] accel_data Pointer to accelerometer data structure (can be NULL for test data)
 ****************************************************************************************
 */
uint8_t notify_accel_data(const accel_sensitivity_t* sens, const accel_data_t *accel_data); 

void update_btn_data(uint8_t btn_pressed); 

void notify_btn_data(uint8_t btn_pressed); 

void update_batt_data(uint32_t batt); 
void notify_batt_data(uint32_t batt); 

#endif

#endif // _USER_CUSTS1_IMPL_H_
