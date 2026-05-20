/**
 ****************************************************************************************
 *
 * @file user_custs1_def.h
 *
 * @brief Custom Server 1 (CUSTS1) profile database definitions.
 *
 * Copyright (c) 2012-2021 Renesas Electronics Corporation and/or its affiliates
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

#ifndef _USER_CUSTS1_DEF_H_
#define _USER_CUSTS1_DEF_H_

/**
 ****************************************************************************************
 * @defgroup USER_CONFIG
 * @ingroup USER
 * @brief Custom Server 1 (CUSTS1) profile database definitions.
 *
 * @{
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "attm_db_128.h"

/*
 * DEFINES
 ****************************************************************************************
 */

// Service 1 of the custom server 1
#define DEF_SVC1_UUID_128                  {0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00}

#define DEF_SVC1_CTRL_POINT_UUID_128       {0x20, 0xEE, 0x8D, 0x0C, 0xE1, 0xF0, 0x4A, 0x0C, 0xB3, 0x25, 0xDC, 0x53, 0x6A, 0x68, 0x86, 0x2D}

#define DEF_SVC1_CTRL_POINT_CHAR_LEN       1

#define DEF_SVC1_CTRL_POINT_USER_DESC      "Control Point"


#define DEF_SVC1_ACCEL_UUID_128       {0xCC, 0x65, 0x9F, 0xC1, 0x38, 0x8B, 0x10, 0x4C, 0xA2, 0x8D, 0xE5, 0x4C, 0x65, 0x91, 0x90, 0xF2}

#define DEF_SVC1_ACCEL_CHAR_LEN       7

#define DEF_SVC1_ACCEL_USER_DESC      "Accelerometer Data"

#define DEF_SVC1_BTN_UUID_128       {0x54, 0xcd, 0xe5, 0xb5, 0x87, 0x2e, 0x46, 0x3f, 0xa8, 0x1a, 0x4a, 0x15, 0x3d, 0x55, 0x8d, 0x36}

#define DEF_SVC1_BTN_CHAR_LEN        1

#define DEF_SVC1_BTN_USER_DESC       "Button Pressed"

#define DEF_SVC1_BATT_UUID_128      {0x8a, 0x67, 0x3e, 0x35, 0xc0, 0x08, 0x48, 0x70, 0x8a, 0x0d, 0x06, 0x80, 0xc3, 0x75, 0x51, 0xe1}

#define DEF_SVC1_BATT_CHAR_LEN       4

#define DEF_SVC1_BATT_USER_DESC     "Battery level"


/// Custom1 Service Data Base Characteristic enum
enum
{
    // Custom Service 1
    SVC1_IDX_SVC = 0,

    SVC1_IDX_CTRL_POINT_CHAR,
    SVC1_IDX_CTRL_POINT_VAL,
    SVC1_IDX_CTRL_POINT_USER_DESC,

    SVC1_IDX_ACCEL_CHAR, 
    SVC1_IDX_ACCEL_VAL, 
    SVC1_IDX_ACCEL_NTF_CFG,
    SVC1_IDX_ACCEL_USER_DESC,

    SVC1_IDX_BTN_CHAR, 
    SVC1_IDX_BTN_VAL, 
    SVC1_IDX_BTN_NTF_CFG, 
    SVC1_IDX_BTN_USER_DESC,

    SVC1_IDX_BATT_CHAR, 
    SVC1_IDX_BATT_VAL, 
    SVC1_IDX_BATT_NTF_CFG, 
    SVC1_IDX_BATT_USER_DESC,

    CUSTS1_IDX_NB
};

/// @} USER_CONFIG

#endif // _USER_CUSTS1_DEF_H_
