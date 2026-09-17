# SPDX-License-Identifier: BSD-3-Clause
"""CC2530 target debug facts from SWRU191F, not USB adapter bytecode."""

from enum import IntFlag


WRITE_CONFIG = 0x1D
READ_CONFIG = 0x24
GET_PC = 0x28
READ_STATUS = 0x34
SET_HW_BREAKPOINT = 0x3F
HALT = 0x44
RESUME = 0x4C
DEBUG_INSTR = 0x54
STEP_INSTR = 0x5C
GET_BM = 0x64


class Status(IntFlag):
    STACK_OVERFLOW = 0x01
    OSCILLATOR_STABLE = 0x02
    DEBUG_LOCKED = 0x04
    HALT_STATUS = 0x08
    PM_ACTIVE = 0x10
    CPU_HALTED = 0x20
    PCON_IDLE = 0x40
    CHIP_ERASE_BUSY = 0x80


def unsigned(value: int, maximum: int, name: str) -> None:
    if type(value) is not int or not 0 <= value <= maximum:
        raise ValueError(f"{name} must be an integer in 0..{maximum}")


def decode_status(value: int) -> dict:
    unsigned(value, 255, "debug status")
    return {"raw": value, **{flag.name.lower(): bool(value & flag) for flag in Status}}


def decode_config(value: int) -> dict:
    unsigned(value, 255, "debug config")
    return {
        "raw": value,
        "soft_power_mode": bool(value & 0x20),
        "timers_off": bool(value & 0x08),
        "dma_pause": bool(value & 0x04),
        "timer_suspend": bool(value & 0x02),
        "reserved_bits": value & 0xD1,
    }


def breakpoint_parameters(slot: int, address: int, bank: int = 0, enabled: bool = True) -> bytes:
    """Encode the three target-side parameter bytes, not an executable USB packet."""
    unsigned(slot, 3, "breakpoint slot")
    unsigned(address, 0xFFFF, "CPU CODE address")
    unsigned(bank, 7, "memory bank")
    if type(enabled) is not bool:
        raise ValueError("enabled must be a boolean")
    return bytes([(slot << 4) | (int(enabled) << 3) | bank, address >> 8, address & 255])
