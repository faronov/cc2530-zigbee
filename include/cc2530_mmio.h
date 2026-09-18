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

/* Public CC2530 SFR addresses; declarations do not enable peripherals. */
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
    X(SOC_MEMCTR, 0xc7) \
    X(SOC_SLEEPCMD, 0xbe) \
    X(SOC_ST0, 0x95) \
    X(SOC_ST1, 0x96) \
    X(SOC_ST2, 0x97) \
    X(SOC_IRCON, 0xc0) \
    X(SOC_DMAIRQ, 0xd1) \
    X(SOC_DMA1CFGL, 0xd2) \
    X(SOC_DMA1CFGH, 0xd3) \
    X(SOC_DMA0CFGL, 0xd4) \
    X(SOC_DMA0CFGH, 0xd5) \
    X(SOC_DMAARM, 0xd6) \
    X(SOC_DMAREQ, 0xd7) \
    X(SOC_ENCDI, 0xb1) \
    X(SOC_ENCDO, 0xb2) \
    X(SOC_ENCCS, 0xb3) \
    X(SOC_S0CON, 0x98) \
    X(SOC_ADCCON1, 0xb4) \
    X(SOC_RNDL, 0xbc) \
    X(SOC_RNDH, 0xbd) \
    X(SOC_RFD, 0xd9) \
    X(SOC_RFST, 0xe1) \
    X(SOC_RFIRQF0, 0xe9) \
    X(SOC_RFIRQF1, 0x91) \
    X(SOC_RFERRF, 0xbf)

#define SFR_ADDRESS(name, address) name##_ADDRESS = address,
enum cc2530_sfr_address { CC2530_REGISTER_LIST(SFR_ADDRESS) };
#undef SFR_ADDRESS

#if defined(__SDCC)
#define DECLARE_SFR(name, address) __sfr __at(address) name;
/* SDCC's runtime page-register symbol must select MPAGE, not generic 8051 P2. */
__sfr __at(0x93) _XPAGE;
#define MMIO_READ(reg) (reg)
#define MMIO_XREAD(address) (*(const volatile MCU_XDATA uint8_t *)(address))
#define MMIO_XWRITE(address, value) do { \
    *(volatile MCU_XDATA uint8_t *)(address) = (uint8_t)(value); \
} while (0)
#define MMIO_XADDRESS(object) ((uint16_t)(object))
#define MMIO_WRITE(reg, value) do { (reg) = (uint8_t)(value); } while (0)
/* Compound SFR operations preserve unrelated port latch bits on the 8051. */
#define MMIO_CLEAR(reg, mask) do { (reg) &= (uint8_t)~(mask); } while (0)
#define MMIO_SET(reg, mask) do { (reg) |= (uint8_t)(mask); } while (0)
#else
#define DECLARE_SFR(name, address) extern volatile uint8_t name;
uint8_t host_mmio_load(const volatile uint8_t *reg, uint8_t address);
uint8_t host_mmio_xload(uint16_t address);
void host_mmio_xstore(uint16_t address, uint8_t value);
uint16_t host_mmio_xaddress(const volatile void *object);
void host_mmio_system_cycles(uint8_t cycles);
void host_mmio_store(volatile uint8_t *reg, uint8_t address, uint8_t value);
#define MMIO_READ(reg) host_mmio_load(&(reg), reg##_ADDRESS)
#define MMIO_XREAD(address) host_mmio_xload(address)
#define MMIO_XWRITE(address, value) host_mmio_xstore((address), (uint8_t)(value))
#define MMIO_XADDRESS(object) host_mmio_xaddress(object)
#define MMIO_WRITE(reg, value) host_mmio_store(&(reg), reg##_ADDRESS, (uint8_t)(value))
#define MMIO_CLEAR(reg, mask) host_mmio_store(&(reg), reg##_ADDRESS, (uint8_t)((reg) & (uint8_t)~(mask)))
#define MMIO_SET(reg, mask) host_mmio_store(&(reg), reg##_ADDRESS, (uint8_t)((reg) | (mask)))
#endif

CC2530_REGISTER_LIST(DECLARE_SFR)
#undef DECLARE_SFR

#endif
