/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "aes.h"
#include "timebase.h"
#include <stddef.h>

/* SWRU191F 2.2.2-2.2.3 pp.27-29; 8.3 pp.97-98; 15.9-15.10 pp.150-151. */
volatile MCU_XDATA uint8_t aes_dma0[8], aes_dma1[32];
volatile MCU_XDATA uint8_t aes_key[16], aes_iv[16], aes_input[16], aes_output[16];
MCU_XDATA uint8_t aes_fault, aes_used;
extern MCU_XDATA uint8_t aes_reserved_end, _gptrput_PARM_2;

typedef struct {
    uint32_t start, previous, deadline;
    uint16_t limit;
    uint8_t clock, ircon, enc_upper, control, cfg0_low, cfg0_high, cfg1_low, cfg1_high;
} aes_wait_t;
static MCU_XDATA aes_wait_t w;

static uint32_t pointer_location(const uint8_t *p)
{
#if defined(__SDCC)
    union { const uint8_t *pointer; uint8_t byte[3]; } value;
    value.pointer = p;
    return (uint32_t)value.byte[0] | ((uint32_t)value.byte[1] << 8) | ((uint32_t)value.byte[2] << 16);
#else
    extern uint32_t host_aes_pointer(const uint8_t *object);
    return host_aes_pointer(p);
#endif
}

static uint8_t overlaps(uint16_t a, uint8_t an, uint16_t b, uint8_t bn)
{
    return a < b ? b - a < an : a - b < bn;
}

static uint8_t owned(uint16_t address, uint8_t count)
{
    return address > MMIO_XADDRESS(&aes_reserved_end) &&
           !overlaps(address, count, MMIO_XADDRESS(&_gptrput_PARM_2), 1);
}

static uint8_t ordinary(uint16_t address, uint8_t count)
{
    return address < 0x1e00u && count <= 0x1e00u - address;
}

static aes_result_t source_range(uint32_t location)
{
    if (location >> 16 == 0x80u)
        return (uint16_t)location <= 0x7ff0u ? AES_OK : AES_INVALID_RANGE;
    if (location >> 16 != 0)
        return AES_INVALID_RANGE;
    if (!ordinary((uint16_t)location, 16))
        return AES_INVALID_RANGE;
    return owned((uint16_t)location, 16) ? AES_OK : AES_BUFFER_OWNERSHIP;
}

#if defined(__SDCC)
/* SWRU191F 8.1 p.93 and Table 2-3 p.39: one fetch at a time, >=9 SYS clocks. */
static void arm_input(void) __naked
{
    __asm
        mov _SOC_DMAARM,#1
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        ret
    __endasm;
}
static void arm_output(void) __naked
{
    __asm
        mov _SOC_DMAARM,#2
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        ret
    __endasm;
}
#else
static void arm_input(void) { MMIO_WRITE(SOC_DMAARM, 1); host_mmio_system_cycles(9); }
static void arm_output(void) { MMIO_WRITE(SOC_DMAARM, 2); host_mmio_system_cycles(9); }
#endif

static aes_result_t observe(aes_diagnostics_t MCU_XDATA *d)
{
    uint8_t ien0, ien1, ien2, sleep, command, status;
    d->sample_valid = 0;
    ien0 = MMIO_READ(SOC_IEN0);
    ien1 = MMIO_READ(SOC_IEN1);
    ien2 = MMIO_READ(SOC_IEN2);
    sleep = MMIO_READ(SOC_SLEEPCMD);
    command = MMIO_READ(SOC_CLKCONCMD);
    status = MMIO_READ(SOC_CLKCONSTA);
    if (ien0 || ien1 || ien2 || (sleep & 7u) != 4u || status != command ||
        (command & 7u) != ((command & 0x40u) ? 1u : 0u) ||
        ((command & 0x40u) && !(command & 0x38u)))
        return AES_UNSUPPORTED_STATE;
    if (command != w.clock)
        return AES_STATE_CHANGED;
    d->control = MMIO_READ(SOC_ENCCS);
    d->enc_flags = MMIO_READ(SOC_S0CON);
    d->arm = MMIO_READ(SOC_DMAARM);
    d->request = MMIO_READ(SOC_DMAREQ);
    d->irq = MMIO_READ(SOC_DMAIRQ);
    d->ircon = MMIO_READ(SOC_IRCON);
    d->cfg0_low = MMIO_READ(SOC_DMA0CFGL);
    d->cfg0_high = MMIO_READ(SOC_DMA0CFGH);
    d->cfg1_low = MMIO_READ(SOC_DMA1CFGL);
    d->cfg1_high = MMIO_READ(SOC_DMA1CFGH);
    d->sample_valid = 1;
    return AES_OK;
}

static aes_result_t sample(aes_diagnostics_t MCU_XDATA *d, uint8_t final)
{
    aes_result_t result;
    uint32_t now;
    bool expired;
    if (d->polls == w.limit)
        return AES_POLL_LIMIT;
    d->polls++;
    result = observe(d);
    if (result != AES_OK)
        return result;
    if ((d->arm & 0xfcu) || d->request || (d->irq & 0xfcu) || d->ircon != w.ircon ||
        (d->enc_flags & 0xfcu) != w.enc_upper || (d->control & 0xf6u) != w.control ||
        d->cfg0_low != w.cfg0_low || d->cfg0_high != w.cfg0_high ||
        d->cfg1_low != w.cfg1_low || d->cfg1_high != w.cfg1_high)
        return AES_STATE_CHANGED;
    if (!d->submitted && d->control != (aes_used ? 0x48 : 8))
        return AES_STATE_CHANGED;
    if (d->phase && d->phase <= 3 && (d->submitted & (1u << (d->phase - 1u)))) {
        if (!(d->arm & 1u) && (d->irq & 1u))
            d->input_complete |= 1u << (d->phase - 1u);
        if (d->phase == 3 && !(d->arm & 2u) && (d->irq & 2u))
            d->output_drained = 1;
    }
    now = timebase_read_awake_ticks24();
    d->elapsed_ticks = (now - w.start) & TIMEBASE_TICKS_MASK;
    d->sample_valid |= 2;
    d->timebase_status = timebase_expired(now, w.deadline, &expired);
    if (d->timebase_status != TIMEBASE_OK)
        return AES_TIMEBASE_ERROR;
    if (d->elapsed_ticks >= TIMEBASE_HALF_RANGE ||
        ((now - w.previous) & TIMEBASE_TICKS_MASK) >= TIMEBASE_HALF_RANGE)
        return AES_COUNTER_RANGE;
    if (expired)
        return AES_TIMEOUT;
    if (d->polls == w.limit && !final)
        return AES_POLL_LIMIT;
    w.previous = now;
    return AES_OK;
}

static void input_descriptor(uint16_t source)
{
    aes_dma0[0] = (uint8_t)(source >> 8); aes_dma0[1] = (uint8_t)source;
    aes_dma0[2] = 0x70; aes_dma0[3] = 0xb1;
    aes_dma0[4] = 0; aes_dma0[5] = 16; aes_dma0[6] = 29; aes_dma0[7] = 0x41;
}

aes_result_t aes128_encrypt_block(const uint8_t *key, const uint8_t *input,
                                  uint8_t MCU_XDATA *output, uint32_t timeout,
                                  uint16_t limit, aes_diagnostics_t MCU_XDATA *d)
{
    uint32_t key_location, input_location;
    uint16_t out, diag, address;
    uint8_t i, bit, command;
    aes_result_t result;
    if (aes_fault)
        return (aes_result_t)aes_fault;
    if (key == NULL || input == NULL || output == NULL || d == NULL || !timeout ||
        timeout >= TIMEBASE_HALF_RANGE || !limit)
        return AES_INVALID_ARGUMENT;
    key_location = pointer_location(key); input_location = pointer_location(input);
    result = source_range(key_location);
    if (result != AES_OK) return result;
    result = source_range(input_location);
    if (result != AES_OK) return result;
    out = MMIO_XADDRESS(output); diag = MMIO_XADDRESS(d);
    if (!ordinary(out, 16) || !ordinary(diag, sizeof(*d)))
        return AES_INVALID_RANGE;
    if (!owned(out, 16) || !owned(diag, sizeof(*d)) || overlaps(out, 16, diag, sizeof(*d)) ||
        (!(key_location >> 16) && (overlaps((uint16_t)key_location, 16, out, 16) ||
                                 overlaps((uint16_t)key_location, 16, diag, sizeof(*d)))) ||
        (!(input_location >> 16) && overlaps((uint16_t)input_location, 16, diag, sizeof(*d))))
        return AES_BUFFER_OWNERSHIP;
    for (i = 0; i < sizeof(*d); i++) ((uint8_t MCU_XDATA *)d)[i] = 0;
    w.clock = MMIO_READ(SOC_CLKCONCMD);
    result = observe(d);
    if (result != AES_OK) goto failed;
    if (d->arm || !(d->control & 8u) || (d->control & 1u)) { result = AES_BUSY; goto failed; }
    if (d->request || d->irq || (d->ircon & 1u) || (d->enc_flags & 3u)) { result = AES_PENDING; goto failed; }
    address = aes_used ? MMIO_XADDRESS(aes_dma0) : 0;
    if (d->control != (aes_used ? 0x48 : 8) || d->cfg0_low != (uint8_t)address ||
        d->cfg0_high != (uint8_t)(address >> 8)) { result = AES_UNSUPPORTED_STATE; goto failed; }
    address = aes_used ? MMIO_XADDRESS(aes_dma1) : 0;
    if (d->cfg1_low != (uint8_t)address || d->cfg1_high != (uint8_t)(address >> 8)) {
        result = AES_UNSUPPORTED_STATE; goto failed;
    }
    w.control = d->control & 0xf6u; w.ircon = d->ircon; w.enc_upper = d->enc_flags;
    w.cfg0_low = d->cfg0_low; w.cfg0_high = d->cfg0_high;
    w.cfg1_low = d->cfg1_low; w.cfg1_high = d->cfg1_high;
    w.start = timebase_read_awake_ticks24(); w.previous = w.start; w.limit = limit;
    d->timebase_status = timebase_deadline_after(w.start, timeout, &w.deadline);
    if (d->timebase_status != TIMEBASE_OK) { result = AES_TIMEBASE_ERROR; goto failed; }
    for (i = 0; i < 16; i++) { aes_key[i] = key[i]; aes_input[i] = input[i]; aes_iv[i] = 0; }
    result = sample(d, 0);
    if (result != AES_OK) goto failed;
    if (d->arm || d->irq || d->control & 1u || d->enc_flags != w.enc_upper) {
        result = AES_STATE_CHANGED; goto failed;
    }
    input_descriptor(MMIO_XADDRESS(aes_key));
    for (i = 0; i < 32; i++) aes_dma1[i] = 0;
    address = MMIO_XADDRESS(aes_output);
    aes_dma1[0] = 0x70; aes_dma1[1] = 0xb2;
    aes_dma1[2] = (uint8_t)(address >> 8); aes_dma1[3] = (uint8_t)address;
    aes_dma1[4] = 0; aes_dma1[5] = 16; aes_dma1[6] = 30; aes_dma1[7] = 0x11;
    address = MMIO_XADDRESS(aes_dma0);
    MMIO_WRITE(SOC_DMA0CFGH, address >> 8); MMIO_WRITE(SOC_DMA0CFGL, address);
    d->configured = 1; w.cfg0_low = (uint8_t)address; w.cfg0_high = (uint8_t)(address >> 8);
    address = MMIO_XADDRESS(aes_dma1);
    MMIO_WRITE(SOC_DMA1CFGH, address >> 8); MMIO_WRITE(SOC_DMA1CFGL, address);
    d->configured = 3; w.cfg1_low = (uint8_t)address; w.cfg1_high = (uint8_t)(address >> 8);
    result = sample(d, 0);
    if (result != AES_OK) goto failed;
    if (d->arm || d->irq || d->enc_flags != w.enc_upper) { result = AES_STATE_CHANGED; goto failed; }
    arm_output(); d->arms++;
    result = sample(d, 0);
    if (result != AES_OK) goto failed;
    if (d->arm != 2 || d->irq || d->enc_flags != w.enc_upper) { result = AES_STATE_CHANGED; goto failed; }
    for (d->phase = 1; d->phase <= 3; d->phase++) {
        bit = 1u << (d->phase - 1u);
        if (d->phase != 1) {
            input_descriptor(d->phase == 2 ? MMIO_XADDRESS(aes_iv) : MMIO_XADDRESS(aes_input));
            result = sample(d, 0);
            if (result != AES_OK) goto failed;
            if (d->arm != 2 || d->irq || d->enc_flags != w.enc_upper || (d->control & 1u)) {
                result = AES_STATE_CHANGED; goto failed;
            }
        }
        arm_input(); d->arms++;
        result = sample(d, 0);
        if (result != AES_OK) goto failed;
        if (d->arm != 3 || d->irq || d->enc_flags != w.enc_upper || (d->control & 1u)) {
            result = AES_STATE_CHANGED; goto failed;
        }
        command = d->phase == 1 ? 0x45 : d->phase == 2 ? 0x47 : 0x41;
        MMIO_WRITE(SOC_ENCCS, command);
        w.control = command & 0xf6u; d->submitted |= bit;
        for (;;) {
            result = sample(d, 0);
            if (result != AES_OK) goto failed;
            if ((d->enc_flags & 3u) != 0 && (d->enc_flags & 3u) != 3) {
                result = AES_STATE_CHANGED; goto failed;
            }
            if (d->phase != 3) {
                if (!(d->arm & 2u) || (d->irq & 2u)) {
                    result = AES_STATE_CHANGED; goto failed;
                }
                if ((d->input_complete & bit) && !(d->control & 1u) && (d->enc_flags & 3u) == 3) break;
            } else {
                if (d->input_complete == 7 && d->output_drained && !d->arm && d->irq == 3 &&
                    (d->control & 9u) == 8 && (d->enc_flags & 3u) == 3) break;
            }
        }
        if (d->phase == 3) MMIO_WRITE(SOC_DMAIRQ, 0x1c);
        else MMIO_WRITE(SOC_DMAIRQ, 0x1e);
        d->ack_issued++;
        result = sample(d, 0);
        if (result != AES_OK) goto failed;
        if (d->irq || d->arm != (d->phase == 3 ? 0 : 2) || (d->control & 1u) ||
            (d->phase == 3 && d->control != 0x48) ||
            d->enc_flags != (uint8_t)(w.enc_upper | 3u)) {
            result = AES_STATE_CHANGED; goto failed;
        }
        d->dma_acked |= bit;
        if (d->phase != 3) {
            /* Own the completed load's flags, not a future command's flags.
             * S0CON, including its reserved high bits, is ordinary R/W (p.46).
             */
            MMIO_WRITE(SOC_S0CON, w.enc_upper); d->enc_ack_issued++;
            result = sample(d, 0);
            if (result != AES_OK) goto failed;
            if (d->arm != 2 || d->irq || (d->control & 1u) || d->enc_flags != w.enc_upper) {
                result = AES_STATE_CHANGED; goto failed;
            }
            d->enc_acked |= bit;
        }
    }
    /* S0CON is R/W, including reserved high bits (SWRU191F p.46).
     * Both fresh ENCIF bits are owned; AES is finished and output fully drained.
     */
    MMIO_WRITE(SOC_S0CON, w.enc_upper); d->enc_ack_issued++;
    result = sample(d, 1);
    if (result != AES_OK) goto failed;
    if (d->arm || d->irq || d->control != 0x48 || d->enc_flags != w.enc_upper) {
        result = AES_STATE_CHANGED; goto failed;
    }
    d->enc_acked |= 4u;
    for (i = 0; i < 16; i++) output[i] = aes_output[i];
    d->published = 1; aes_used = 1;
    return AES_OK;
failed:
    aes_fault = result;
    return result;
}

MCU_XDATA uint8_t aes_reserved_end;
