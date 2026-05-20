/**
 ****************************************************************************************
 *
 * @file user_custs1_def.c
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

/**
 ****************************************************************************************
 * @defgroup USER_CONFIG
 * @ingroup USER
 * @brief Custom server 1 (CUSTS1) profile database definitions.
 *
 * @{
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */

#include <stdint.h>
#include "co_utils.h"
#include "prf_types.h"
#include "attm_db_128.h"
#include "user_custs1_def.h"
#include "attm.h"
#include "app_prf_types.h"

/*
 * LOCAL VARIABLE DEFINITIONS
 ****************************************************************************************
 */

// Service 1 of the custom server 1
static const att_svc_desc128_t custs1_svc1 = DEF_SVC1_UUID_128;

// led control point
static const uint8_t SVC1_CTRL_POINT_UUID_128[ATT_UUID_128_LEN] = DEF_SVC1_CTRL_POINT_UUID_128;

// accel data
static const uint8_t SVC1_ACCEL_UUID_128[ATT_UUID_128_LEN] = DEF_SVC1_ACCEL_UUID_128; 

// button data
static const uint8_t SVC1_BTN_UUID_128[ATT_UUID_128_LEN] = DEF_SVC1_BTN_UUID_128; 

static const uint8_t SVC1_BATT_UUID_128[ATT_UUID_128_LEN] = DEF_SVC1_BATT_UUID_128; 


// Attribute specifications
static const uint16_t att_decl_svc       = ATT_DECL_PRIMARY_SERVICE;
static const uint16_t att_decl_char      = ATT_DECL_CHARACTERISTIC;
static const uint16_t att_desc_cfg       = ATT_DESC_CLIENT_CHAR_CFG;
static const uint16_t att_desc_user_desc = ATT_DESC_CHAR_USER_DESCRIPTION;

/*
 * GLOBAL VARIABLE DEFINITIONS
 ****************************************************************************************
 */

const uint8_t custs1_services[]  = {SVC1_IDX_SVC, CUSTS1_IDX_NB};
const uint8_t custs1_services_size = ARRAY_LEN(custs1_services) - 1;
const uint16_t custs1_att_max_nb = CUSTS1_IDX_NB;

/// Full CUSTS1 Database Description - Used to add attributes into the database
const struct attm_desc_128 custs1_att_db[CUSTS1_IDX_NB] =
{
    /*************************
     * Service 1 configuration
     *************************
     */

    // Service 1 Declaration
    [SVC1_IDX_SVC]                      = {(uint8_t*)&att_decl_svc, ATT_UUID_128_LEN, PERM(RD, ENABLE),
                                            sizeof(custs1_svc1), sizeof(custs1_svc1), (uint8_t*)&custs1_svc1},

    // Control Point Characteristic Declaration
    [SVC1_IDX_CTRL_POINT_CHAR]        = {(uint8_t*)&att_decl_char, ATT_UUID_16_LEN, PERM(RD, ENABLE),
                                            0, 0, NULL},

    // Control Point Characteristic Value
    [SVC1_IDX_CTRL_POINT_VAL]         = {SVC1_CTRL_POINT_UUID_128, ATT_UUID_128_LEN, PERM(RD, ENABLE) | PERM(WR, ENABLE) | PERM(WRITE_REQ, ENABLE),
                                            DEF_SVC1_CTRL_POINT_CHAR_LEN, 0, NULL},

    // Control Point Characteristic User Description
    [SVC1_IDX_CTRL_POINT_USER_DESC]   = {(uint8_t*)&att_desc_user_desc, ATT_UUID_16_LEN, PERM(RD, ENABLE),
                                            sizeof(DEF_SVC1_CTRL_POINT_USER_DESC) - 1, sizeof(DEF_SVC1_CTRL_POINT_USER_DESC) - 1, (uint8_t*)DEF_SVC1_CTRL_POINT_USER_DESC},

    // Accelerometer Characteristic Declaration
    [SVC1_IDX_ACCEL_CHAR]        = {(uint8_t*)&att_decl_char, ATT_UUID_16_LEN, PERM(RD, ENABLE),
                                            0, 0, NULL},

    // Accelerometer Characteristic Value
    [SVC1_IDX_ACCEL_VAL]         = {SVC1_ACCEL_UUID_128, ATT_UUID_128_LEN, PERM(RD, ENABLE) | PERM(WR, ENABLE) | PERM(WRITE_REQ, ENABLE) | PERM(NTF, ENABLE),
                                            DEF_SVC1_ACCEL_CHAR_LEN, 0, NULL},

    // Accelerometer Characteristic NTF CCCD
    [SVC1_IDX_ACCEL_NTF_CFG]     = {(uint8_t*)&att_desc_cfg,ATT_UUID_16_LEN, PERM(RD, ENABLE) | PERM(WR, ENABLE) | PERM(WRITE_REQ, ENABLE),
                                       sizeof(uint16_t),
                                       0,
                                       NULL},

    // Accelerometer Characteristic User Description
    [SVC1_IDX_ACCEL_USER_DESC]   = {(uint8_t*)&att_desc_user_desc, ATT_UUID_16_LEN, PERM(RD, ENABLE),
                                            sizeof(DEF_SVC1_ACCEL_USER_DESC) - 1, sizeof(DEF_SVC1_ACCEL_USER_DESC) - 1, (uint8_t*)DEF_SVC1_ACCEL_USER_DESC},


    // Button Characteristic Declaration
    [SVC1_IDX_BTN_CHAR]        = {(uint8_t*)&att_decl_char, ATT_UUID_16_LEN, PERM(RD, ENABLE),
                                            0, 0, NULL},

    // Button Characteristic Value
    [SVC1_IDX_BTN_VAL]         = {SVC1_BTN_UUID_128, ATT_UUID_128_LEN, PERM(RD, ENABLE) | PERM(WR, ENABLE) | PERM(WRITE_REQ, ENABLE) | PERM(NTF, ENABLE),
                                            DEF_SVC1_BTN_CHAR_LEN, 0, NULL},

    // Button Characteristic NTF CCCD
    [SVC1_IDX_BTN_NTF_CFG]     = {(uint8_t*)&att_desc_cfg,ATT_UUID_16_LEN, PERM(RD, ENABLE) | PERM(WR, ENABLE) | PERM(WRITE_REQ, ENABLE),
                                       sizeof(uint16_t),
                                       0,
                                       NULL},

    // Button Characteristic User Description
    [SVC1_IDX_BTN_USER_DESC]   = {(uint8_t*)&att_desc_user_desc, ATT_UUID_16_LEN, PERM(RD, ENABLE),
                                            sizeof(DEF_SVC1_BTN_USER_DESC) - 1, sizeof(DEF_SVC1_BTN_USER_DESC) - 1, (uint8_t*)DEF_SVC1_BTN_USER_DESC},

    // Battery Characteristic Declaration
    [SVC1_IDX_BATT_CHAR]        = {(uint8_t*)&att_decl_char, ATT_UUID_16_LEN, PERM(RD, ENABLE),
                                            0, 0, NULL},

    // Battery Characteristic Value
    [SVC1_IDX_BATT_VAL]         = {SVC1_BATT_UUID_128, ATT_UUID_128_LEN, PERM(RD, ENABLE) | PERM(WR, ENABLE) | PERM(WRITE_REQ, ENABLE) | PERM(NTF, ENABLE),
                                            DEF_SVC1_BATT_CHAR_LEN, 0, NULL},

    // Button Characteristic NTF CCCD
    [SVC1_IDX_BATT_NTF_CFG]     = {(uint8_t*)&att_desc_cfg,ATT_UUID_16_LEN, PERM(RD, ENABLE) | PERM(WR, ENABLE) | PERM(WRITE_REQ, ENABLE),
                                       sizeof(uint16_t),
                                       0,
                                       NULL},

    // Button Characteristic User Description
    [SVC1_IDX_BATT_USER_DESC]   = {(uint8_t*)&att_desc_user_desc, ATT_UUID_16_LEN, PERM(RD, ENABLE),
                                            sizeof(DEF_SVC1_BATT_USER_DESC) - 1, sizeof(DEF_SVC1_BATT_USER_DESC) - 1, (uint8_t*)DEF_SVC1_BATT_USER_DESC},
};

/// @} USER_CONFIG
