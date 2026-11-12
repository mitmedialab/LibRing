// Ring-to-ring RSSI ("proximity").
//
// While the client listens to the Proximity characteristic, the ring alternates between
// - non-connectable advertising, so that the other rings hear it (connected rings stop
//   advertising otherwise), and
// - passive scanning, recording the RSSI of the advertisements of every LibRing around
//   (rings in this mode, and rings not connected, which advertise anyway).
// The stack runs one air operation at a time (GAPM_OP_AIR): hence the alternation, with
// random phase lengths so that two rings don't stay in step (both advertising, or both
// scanning). The connection to the client goes on meanwhile.
// After each scanning phase, a notification reports what was heard (proximity.h).
//
// Safety: on a disconnection, the normal (connectable) advertising can only restart once
// the current air operation has ended; a timer forces it in case that never happens, so
// that the ring stays reachable.

#include <string.h>
#include "proximity.h"
#include "app.h"
#include "app_easy_gap.h"
#include "app_easy_timer.h"
#include "app_default_handlers.h"
#include "co_math.h"
#include "custs1_task.h"
#include "user_custs1_def.h"
#include "prf_utils.h"
#include "arch_console.h"

// Phase lengths (10 ms units): advertising 50-100 ms, scanning 80-160 ms (~5 reports/s)
#define ADV_MIN       5
#define ADV_SPREAD    6
#define SCAN_MIN      8
#define SCAN_SPREAD   9
#define ADV_INTV_MS   20    // advertising interval during the advertising phase (BLE 5 minimum)
#define SCAN_SLOTS    0x40  // scan interval = window = 40 ms (0.625 ms units): continuous
#define RETRY_DELAY   10    // 100 ms after a failed air operation
#define RESTART_GUARD 100   // 1 s: connectable advertising restarted at the latest

enum air_op { AIR_NONE, AIR_ADV, AIR_SCAN };

static bool enabled;
static enum air_op air = AIR_NONE;     // air operation started by this module
static enum air_op last = AIR_SCAN;    // last phase (the next one is the other)
static bool restart_adv;               // disconnected: restart normal advertising
static timer_hnd phase_timer = EASY_TIMER_INVALID_TIMER;
static timer_hnd guard_timer = EASY_TIMER_INVALID_TIMER;
static uint8_t errors;                 // failed air operations (saturating)
static uint8_t seq;                    // report number (the client counts lost reports)

static struct {
    uint8_t id;      // last byte of the ring's address (= XX in its name LibRing_XX)
    uint8_t count;
    int16_t sum;
} peers[PROX_MAX_PEERS];
static uint8_t n_peers;

static void phase_timer_cb(void);

static void cancel_timer(timer_hnd *t)
{
    if (*t != EASY_TIMER_INVALID_TIMER) {
        app_easy_timer_cancel(*t);
        *t = EASY_TIMER_INVALID_TIMER;
    }
}

// Sends what was heard since the last report. When the service is busy (e.g. with an
// accelerometer notification), the data is kept for the next report instead.
static void send_report(void)
{
    if (PRF_ENV_GET(CUSTS1, custs1) == NULL || ke_state_get(TASK_ID_CUSTS1) == CUSTS1_BUSY) {
        return;
    }
    uint8_t buf[PROX_REPORT_LEN];
    uint8_t len = 3;
    buf[0] = n_peers;
    buf[1] = errors;
    buf[2] = seq;
    for (uint8_t i = 0; i < n_peers; i++) {
        buf[len++] = peers[i].id;
        buf[len++] = (uint8_t)(int8_t)(peers[i].sum / peers[i].count);
        buf[len++] = peers[i].count;
    }
    struct custs1_val_ntf_ind_req *req = KE_MSG_ALLOC_DYN(CUSTS1_VAL_NTF_REQ,
                                                          prf_get_task_from_id(TASK_ID_CUSTS1),
                                                          TASK_APP, custs1_val_ntf_ind_req, len);
    if (req == NULL) {
        return;
    }
    n_peers = 0;
    seq++;
    req->conidx = 0;
    req->handle = SVC1_IDX_PROX_VAL;
    req->length = len;
    req->notification = true;
    memcpy(req->value, buf, len);
    ke_msg_send(req);

    // Also the value of the characteristic, for a client that reads it: Chrome on Linux
    // sometimes stops delivering the notifications (the report number avoids duplicates)
    struct custs1_val_set_req *set = KE_MSG_ALLOC_DYN(CUSTS1_VAL_SET_REQ,
                                                      prf_get_task_from_id(TASK_ID_CUSTS1),
                                                      TASK_APP, custs1_val_set_req, len);
    if (set != NULL) {
        set->conidx = 0;
        set->handle = SVC1_IDX_PROX_VAL;
        set->length = len;
        memcpy(set->value, buf, len);
        ke_msg_send(set);
    }
}

static void start_adv(void)
{
    // Same advertising data as normally (it carries the name LibRing_XX), not connectable
    struct gapm_start_advertise_cmd *cmd = app_easy_gap_non_connectable_advertise_get_active();
    cmd->intv_min = MS_TO_BLESLOTS(ADV_INTV_MS);
    cmd->intv_max = MS_TO_BLESLOTS(ADV_INTV_MS);
    app_easy_gap_non_connectable_advertise_start();
    air = AIR_ADV;
}

static void start_scan(void)
{
    struct gapm_start_scan_cmd *cmd = KE_MSG_ALLOC(GAPM_START_SCAN_CMD, TASK_GAPM, TASK_APP,
                                                   gapm_start_scan_cmd);
    memset(cmd, 0, sizeof(*cmd));
    cmd->op.code = GAPM_SCAN_PASSIVE;
    cmd->op.addr_src = GAPM_STATIC_ADDR;
    cmd->interval = SCAN_SLOTS;
    cmd->window = SCAN_SLOTS;
    cmd->mode = GAP_OBSERVER_MODE;
    cmd->filt_policy = SCAN_ALLOW_ADV_ALL;
    cmd->filter_duplic = SCAN_FILT_DUPLIC_DIS; // every advertisement, not the first one only
    KE_MSG_SEND(cmd);
    air = AIR_SCAN;
}

// Starts the phase after the last one, and the timer that ends it
static void next_phase(void)
{
    if (last == AIR_SCAN) {
        start_adv();
        last = AIR_ADV;
        phase_timer = app_easy_timer(ADV_MIN + co_rand_byte() % ADV_SPREAD, phase_timer_cb);
    } else {
        start_scan();
        last = AIR_SCAN;
        phase_timer = app_easy_timer(SCAN_MIN + co_rand_byte() % SCAN_SPREAD, phase_timer_cb);
    }
}

static void phase_timer_cb(void)
{
    phase_timer = EASY_TIMER_INVALID_TIMER;
    if (air != AIR_NONE) {
        app_easy_gap_advertise_stop(); // GAPM_CANCEL_CMD: ends advertising or scanning
    } else if (enabled) {
        next_phase(); // retry after a failed air operation
    }
}

static void restart_normal_adv(void)
{
    cancel_timer(&guard_timer);
    if (restart_adv) {
        restart_adv = false;
        default_advertise_operation();
    }
}

static void guard_timer_cb(void)
{
    guard_timer = EASY_TIMER_INVALID_TIMER;
    if (restart_adv) {
        arch_printf("prox: no end of air operation, advertising restarted anyway\r\n");
        air = AIR_NONE;
        restart_normal_adv();
    }
}

void prox_on_air_op_complete(uint8_t status)
{
    if (air == AIR_NONE) {
        return; // not ours
    }
    enum air_op ended = air;
    air = AIR_NONE;

    if (!enabled) {
        restart_normal_adv(); // if disconnected meanwhile
        return;
    }
    if (ended == AIR_SCAN) {
        send_report();
    }
    if (status == GAP_ERR_NO_ERROR || status == GAP_ERR_CANCELED) {
        cancel_timer(&phase_timer); // ended on its own: don't cancel the next phase
        next_phase();
    } else {
        // e.g. refused by the stack: try the other phase a bit later, without spinning
        if (errors < 255) {
            errors++;
        }
        arch_printf("prox: air operation %d failed: 0x%02x\r\n", ended, status);
        cancel_timer(&phase_timer);
        phase_timer = app_easy_timer(RETRY_DELAY, phase_timer_cb);
    }
}

void prox_enable(bool enable)
{
    if (enable == enabled) {
        return;
    }
    enabled = enable;
    arch_printf("prox: %d\r\n", enable);
    if (enable) {
        n_peers = 0;
        errors = 0;
        last = AIR_SCAN;
        if (air == AIR_NONE) { // else: next phase when the current operation ends
            next_phase();
        }
    } else {
        cancel_timer(&phase_timer);
        if (air != AIR_NONE) {
            app_easy_gap_advertise_stop(); // its completion leaves air at AIR_NONE
        }
    }
}

bool prox_on_disconnect(void)
{
    enabled = false;
    cancel_timer(&phase_timer);
    if (air == AIR_NONE) {
        return false;
    }
    restart_adv = true;
    app_easy_gap_advertise_stop();
    cancel_timer(&guard_timer);
    guard_timer = app_easy_timer(RESTART_GUARD, guard_timer_cb);
    return true;
}

void prox_on_adv_report(struct gapm_adv_report_ind const *param)
{
    if (!enabled) {
        return;
    }
    const struct adv_report *r = &param->report;
    // LibRing rings only: complete local name "LibRing_..." in the advertising data
    bool libring = false;
    for (uint8_t i = 0; i + 1 < r->data_len && i < ADV_DATA_LEN; ) {
        uint8_t len = r->data[i];
        if (len == 0 || i + 1 + len > r->data_len) {
            break;
        }
        uint8_t type = r->data[i + 1];
        if ((type == GAP_AD_TYPE_COMPLETE_NAME || type == GAP_AD_TYPE_SHORTENED_NAME) &&
            len - 1 >= 8 && memcmp(&r->data[i + 2], "LibRing_", 8) == 0) {
            libring = true;
            break;
        }
        i += 1 + len;
    }
    if (!libring) {
        return;
    }

    uint8_t id = r->adv_addr.addr[0];
    uint8_t k;
    for (k = 0; k < n_peers && peers[k].id != id; k++) {
    }
    if (k == n_peers) {
        if (n_peers == PROX_MAX_PEERS) {
            return;
        }
        n_peers++;
        peers[k].id = id;
        peers[k].count = 0;
        peers[k].sum = 0;
    }
    if (peers[k].count < 255) {
        peers[k].count++;
        peers[k].sum += (int8_t)r->rssi;
    }
}
