/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef JOIN_SMOKE_H
#define JOIN_SMOKE_H
#include "bringup.h"
#include "mac_link_driver.h"

#define JOIN_SMOKE_DRAWS 64u
#define JOIN_SMOKE_SERVICE_TICKS 4096UL
#define JOIN_SMOKE_SERVICE_POLLS 512u
#define JOIN_SMOKE_ADMISSION_POLLS 256u
#define JOIN_SMOKE_STEPS 1000000UL
#define JOIN_SMOKE_RAW_TICKS 2949120UL
#define JOIN_SMOKE_SERVE_TICKS 7864320UL

enum { JS_DISARMED=1, JS_ARMED, JS_ADMITTED, JS_RUNNING, JS_READY, JS_FAULT };
enum { JS_NONE, JS_PACKET, JS_ADMISSION_EXPIRED, JS_INPUT, JS_SECURITY,
       JS_ADAPTER, JS_MAC, JS_BDB, JS_DRIVER, JS_RANDOM, JS_TIME, JS_WORK, JS_TERMINAL };

typedef struct {
    bdb_join_config_t join;
    security_keys_config_t identity;
    uint32_t nwk_floor, aps_floor;
    uint8_t install_code[18], qualified_draws;
} join_smoke_admission_t;

/* Start has returned before the initially unbound driver is zeroed and bound.
 * No admission pointer survives. Discovery defers provisioning to association. */
typedef union {
    join_smoke_admission_t admission;
    mac_link_driver_t driver;
} join_smoke_phase_t;

typedef struct {
    uint8_t signature[4], version, size, phase, reason;
    uint8_t stage, consumed, bound, saw_ready;
    uint8_t security_result, adapter_result, mac_result, bdb_result, driver_result;
    uint8_t bdb_phase, bdb_reason, member, adapter_phase, held, ready, goal, normal_rx;
    uint8_t radio_result, radio_phase, radio_errors, draws;
    uint8_t remaining[2], steps[4], elapsed[3], live[4], guards[2], draw_limit;
    uint8_t announce, discarded, dropped;
} join_smoke_status_t;

typedef char join_smoke_status_size[sizeof(join_smoke_status_t)==48 ? 1 : -1];
typedef char join_smoke_admission_size[
    sizeof(join_smoke_admission_t)<=sizeof(mac_link_driver_t) ? 1 : -1];

extern MCU_XDATA bdb_join_t join_smoke_device;
extern MCU_XDATA mac_tx_interval_t join_smoke_tx;
extern MCU_XDATA join_smoke_phase_t join_smoke_phase;
extern MCU_XDATA radio_autoack_config_t join_smoke_initial;
extern MCU_XDATA mac_epoch_stamp_t join_smoke_clock;
extern MCU_XDATA uint8_t join_smoke_draws[JOIN_SMOKE_DRAWS];
extern volatile MCU_XDATA uint8_t join_smoke_mailbox[8];
extern volatile MCU_XDATA MCU_AT(M0_STATUS_ADDRESS) join_smoke_status_t join_smoke_status;

/* Draft, not a flashable/accepted image. Whole-reset, single-use caller.
 * Load public admission storage and externally qualified random bytes while
 * halted DISARMED. ARM: 4A B5 4E B1 A9 56 01 FE.
 * RUN: 4A B5 4E B1 56 A9 01 FE. Both return without RF/NV to WAIT.
 * The continuation after ADMITTED runs real radio/BDB and explicitly creates
 * fresh durable state. The default-TC profile learns network metadata before
 * provisioning; identity.channel is only the initial OFF radio channel, not a
 * network selector. It uses floors1/1, ignores install_code and network identity
 * input fields, and requires the same own IEEE in both public input views.
 * Existing NV is refused, never erased/reprovisioned.
 * READY is authenticated BDB readiness, NOT radio OFF or complete HA interview.
 * READY then serves (stage 5) for JOIN_SMOKE_SERVE_TICKS from a fresh step and
 * time budget: ZDO/Basic requests are answered by the runtime, other published
 * application frames are discarded and counted, and one APS-acknowledged Basic
 * ModelIdentifier report to the coordinator (announce: 255 unsent, else the
 * APS result or 0x80|BDB send failure) invites an interview. Stage 6 means the
 * serving budget ended without a fault; no further driver step is made.
 * READY/FAULT retain ownership; no independent lower-service cleanup is legal.
 * A separately authorized operator must handle physical shutdown/full reset.
 * This image owns the reserved status block as JSN1, not the bootstrap M0 ABI.
 * RUNNING poll calls make bounded foreground progress without a debugger stop.
 * qualified_draws is an input count, not an entropy assessment by firmware.
 * The operator must establish the source and count before ARM; no live refill.
 */
void join_smoke_initialize(void);
void join_smoke_poll(void);
void join_smoke_wait(void);
void join_smoke_ready(void);
void join_smoke_fault(void);
#endif
