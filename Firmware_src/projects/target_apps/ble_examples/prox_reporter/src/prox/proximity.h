// Ring-to-ring RSSI ("proximity"), see proximity.c
#ifndef _PROXIMITY_H_
#define _PROXIMITY_H_

#include <stdbool.h>
#include <stdint.h>
#include "gapm_task.h"

/// Report: [peer count, air-operation errors, report number, then per peer: ID, mean
/// RSSI (dBm, int8), number of advertisements heard] (since the previous report)
#define PROX_MAX_PEERS   16
#define PROX_REPORT_LEN  (3 + 3 * PROX_MAX_PEERS)

/// Client subscribed / unsubscribed (Proximity characteristic CCCD)
void prox_enable(bool enable);

/// Link lost: stops the measurement. Returns true when advertising must wait for the
/// end of the current air operation (prox then restarts it), false when the caller
/// can restart advertising itself.
bool prox_on_disconnect(void);

/// Callbacks (user_callback_config.h)
void prox_on_adv_report(struct gapm_adv_report_ind const *param);
void prox_on_air_op_complete(uint8_t status);

#endif // _PROXIMITY_H_
