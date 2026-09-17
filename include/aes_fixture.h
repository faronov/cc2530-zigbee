/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef AES_FIXTURE_H
#define AES_FIXTURE_H
#include "bringup.h"
#include "aes.h"
#include "clock.h"

#define AEF_SIZE 64u
#define AEF_TIMEOUT 32768UL
#define AEF_LIMIT 4096u
enum aef_phase { AEF_INIT = 1, AEF_RUNNING, AEF_READY, AEF_FAULT };
enum aef_stage { AEF_INITIAL_RC, AEF_AES_RC, AEF_CLOCK_X, AEF_AES_X, AEF_CLOCK_RC };
enum aef_reason { AEF_NONE, AEF_ENTRY, AEF_PHASE, AEF_CLOCK_ERROR, AEF_AES_ERROR, AEF_INVARIANT, AEF_BYTES };
#define AEF_REGISTERS(X) \
    X(AEF_IP0, 0xa9) X(AEF_IP1, 0xb9) X(AEF_TCON, 0x88) X(AEF_S1CON, 0x9b) \
    X(AEF_RFIRQF0, 0xe9) X(AEF_RFIRQF1, 0x91) X(AEF_IRCON2, 0xe8)
#define AEF_ADDRESS(name, address) name##_ADDRESS = address,
enum aef_register_address { AEF_REGISTERS(AEF_ADDRESS) };
#undef AEF_ADDRESS
#if defined(__SDCC)
#define AEF_REGISTER(name, address) __sfr __at(address) name;
#else
#define AEF_REGISTER(name, address) extern volatile uint8_t name;
#endif
AEF_REGISTERS(AEF_REGISTER)
#undef AEF_REGISTER

typedef union { aes_diagnostics_t aes; clock_diagnostics_t clock; } aes_fixture_work_t;
/* M2AE v2: enc_ack_issued is a count; enc_acked is key/IV/block bits 1/2/4. */
typedef struct {
    uint8_t signature[4], version, size, phase, reason, stage, completed, vector, spaces;
    uint8_t result, kind, checked, mismatch_buffer, mismatch_index, actual, expected, fault_latch;
    uint8_t timeout[3], limit[2], diagnostic[19];
    uint8_t command, status, sleep, enables[3], cpu_valid;
    uint8_t initial_flags[8], initial_ircon, initial_enc, initial_sleep, guards[2];
} aes_fixture_t;
typedef char aef_size[(sizeof(aes_fixture_t) == AEF_SIZE) ? 1 : -1];
#define AEF_OFFSET(field, n) typedef char aef_offset_##field[(offsetof(aes_fixture_t, field) == n) ? 1 : -1]
AEF_OFFSET(diagnostic, 25); AEF_OFFSET(command, 44); AEF_OFFSET(initial_flags, 51); AEF_OFFSET(guards, 62);
#undef AEF_OFFSET

extern const MCU_CODE uint8_t aes_fixture_vectors[21][49];
extern volatile MCU_XDATA aes_fixture_t aes_fixture_state;
extern MCU_XDATA uint8_t aes_fixture_key[16], aes_fixture_input[16], aes_fixture_output[18];
extern MCU_XDATA aes_fixture_work_t aes_fixture_work;
void aes_fixture_initialize(void);
#if defined(__SDCC)
void aes_fixture_before(void);
void aes_fixture_ready(void);
void aes_fixture_fault(void);
#else
void aes_fixture_step(void);
#endif
#endif
