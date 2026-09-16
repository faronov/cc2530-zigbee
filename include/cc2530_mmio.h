/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CC2530_MMIO_H
#define CC2530_MMIO_H

#include <stdint.h>

#if defined(__SDCC)
#define MCU_XDATA __xdata
#define MCU_CODE __code
#define MCU_AT(address) __at(address)
#elif defined(CC2530_HOST_TEST)
#define MCU_XDATA
#define MCU_CODE
#define MCU_AT(address)
#else
#error Select SDCC for firmware or CC2530_HOST_TEST for host tests
#endif

/* Public CC2530 SFR addresses; no radio/USART implementation is included. */
#define CC2530_REGISTER_LIST(X) \
    X(SOC_P0, 0x80) \
    X(SOC_P1, 0x90) \
    X(SOC_P2, 0xa0) \
    X(SOC_IEN0, 0xa8) \
    X(SOC_IEN1, 0xb8) \
    X(SOC_IEN2, 0x9a) \
    X(SOC_P0DIR, 0xfd) \
    X(SOC_P1DIR, 0xfe) \
    X(SOC_P2DIR, 0xff) \
    X(SOC_P0SEL, 0xf3) \
    X(SOC_P1SEL, 0xf4) \
    X(SOC_P2SEL, 0xf5) \
    X(SOC_P0INP, 0x8f) \
    X(SOC_P1INP, 0xf6) \
    X(SOC_P2INP, 0xf7) \
    X(SOC_APCFG, 0xf2) \
    X(SOC_PERCFG, 0xf1) \
    X(SOC_CLKCONCMD, 0xc6) \
    X(SOC_CLKCONSTA, 0x9e) \
    X(SOC_SLEEPCMD, 0xbe) \
    X(SOC_ST0, 0x95) \
    X(SOC_ST1, 0x96) \
    X(SOC_ST2, 0x97)

#define SFR_ADDRESS(name, address) name##_ADDRESS = address,
enum cc2530_sfr_address { CC2530_REGISTER_LIST(SFR_ADDRESS) };
#undef SFR_ADDRESS

#if defined(__SDCC)
#define DECLARE_SFR(name, address) __sfr __at(address) name;
/* SDCC's runtime page-register symbol must select MPAGE, not generic 8051 P2. */
__sfr __at(0x93) _XPAGE;
#define MMIO_READ(reg) (reg)
#define MMIO_WRITE(reg, value) do { (reg) = (uint8_t)(value); } while (0)
/* Compound SFR operations preserve unrelated port latch bits on the 8051. */
#define MMIO_CLEAR(reg, mask) do { (reg) &= (uint8_t)~(mask); } while (0)
#define MMIO_SET(reg, mask) do { (reg) |= (uint8_t)(mask); } while (0)
#else
#define DECLARE_SFR(name, address) extern volatile uint8_t name;
uint8_t host_mmio_load(const volatile uint8_t *reg, uint8_t address);
void host_mmio_store(volatile uint8_t *reg, uint8_t address, uint8_t value);
#define MMIO_READ(reg) host_mmio_load(&(reg), reg##_ADDRESS)
#define MMIO_WRITE(reg, value) host_mmio_store(&(reg), reg##_ADDRESS, (uint8_t)(value))
#define MMIO_CLEAR(reg, mask) host_mmio_store(&(reg), reg##_ADDRESS, (uint8_t)((reg) & (uint8_t)~(mask)))
#define MMIO_SET(reg, mask) host_mmio_store(&(reg), reg##_ADDRESS, (uint8_t)((reg) | (mask)))
#endif

CC2530_REGISTER_LIST(DECLARE_SFR)
#undef DECLARE_SFR

#endif
