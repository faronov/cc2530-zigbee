# SPDX-License-Identifier: BSD-3-Clause
"""Strict, dependency-free offline ELF/HEX and selected-startup checks."""

from dataclasses import dataclass
import hashlib
import re
import struct
from typing import Optional

FLASH_END = 0x100000
RAM_START = 0x20000000
RAM_END = RAM_START + 0x40000
FLASH_LIMIT = 0x40000
RAM_LIMIT = 0x10000
RAM_EXEC_LIMIT = 0x20000
STARTUP_HASHES = {
    "SystemInit": "1a98a0a1ab466c71f6cc05ff0cf44fe7393750704bd9667c6470aab8ef3545c6",
    "nrf52_handle_approtect": "57da7eecd9a3111062b8e7a761a15306a156e2bbe8074fa66c51783f0b0940d2",
}


class ArtifactError(ValueError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ArtifactError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _load_bounds(ram_only: bool) -> tuple[int, int]:
    require(type(ram_only) is bool, "image profile must be explicit boolean")
    return ((RAM_START, RAM_START + RAM_EXEC_LIMIT) if ram_only else (0, FLASH_LIMIT))


def ihex(text: str, *, ram_only: bool = False) -> tuple[dict[int, int], Optional[int]]:
    load_start, load_end = _load_bounds(ram_only)
    memory: dict[int, int] = {}
    upper, entry, ended = 0, None, False
    require(len(text) <= 4 * 1024 * 1024, "HEX file too large")
    text = text.replace("\r\n", "\n")
    require("\r" not in text, "HEX requires LF or CRLF record separators")
    lines = text.split("\n")
    if lines[-1] == "":
        lines.pop()
    require(bool(lines), "empty HEX")
    for line in lines:
        require(not ended and re.fullmatch(r":[0-9A-Fa-f]+", line) is not None,
                "data after EOF or invalid HEX characters")
        require(len(line) % 2 == 1 and 11 <= len(line) <= 521, "invalid HEX record size")
        raw = bytes.fromhex(line[1:])
        require(len(raw) == raw[0] + 5 and sum(raw) % 256 == 0, "HEX length/checksum")
        address = int.from_bytes(raw[1:3], "big")
        kind, data = raw[3], raw[4:-1]
        if kind == 0:
            require(bool(data) and address + len(data) <= 0x10000, "empty/wrapped HEX data")
            start = upper + address
            require(load_start <= start < start + len(data) <= load_end,
                    "HEX load outside selected image profile")
            for i, value in enumerate(data, start):
                require(i not in memory, "overlapping HEX data, even identical")
                memory[i] = value
        elif kind == 1:
            require(address == 0 and not data, "malformed EOF")
            ended = True
        elif kind in (2, 4):
            require(address == 0 and len(data) == 2, "malformed extended address")
            upper = int.from_bytes(data, "big") << (4 if kind == 2 else 16)
        elif kind in (3, 5):
            require(address == 0 and len(data) == 4 and entry is None, "malformed/repeated entry")
            entry = (int.from_bytes(data[:2], "big") * 16 + int.from_bytes(data[2:], "big")
                     if kind == 3 else int.from_bytes(data, "big"))
            require(entry & 1 == 1 and load_start <= entry < load_end, "invalid Thumb entry")
        else:
            raise ArtifactError(f"unsupported HEX record type {kind}")
    require(ended and bool(memory), "missing EOF or data")
    return memory, entry


@dataclass(frozen=True)
class Image:
    memory: dict[int, int]
    entry: int
    flash_extent: int
    sram_allocated: int
    sram_extent: int
    sections: list[dict]
    load_start: int
    load_extent: int


def elf(data: bytes, *, ram_only: bool = False) -> Image:
    load_start, load_end = _load_bounds(ram_only)
    ram_limit = RAM_EXEC_LIMIT if ram_only else RAM_LIMIT
    require(52 <= len(data) <= 16 * 1024 * 1024, "invalid ELF size")
    require(data[:7] == b"\x7fELF\x01\x01\x01", "expected ELF32 little-endian")
    fields = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    kind, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, names = fields
    require(kind == 2 and machine == 40 and version == 1 and ehsize == 52, "not an ARM executable")
    require(flags >> 24 == 5 and entry & 1 == 1, "expected EABI5 Thumb")
    require(phsize == 32 and 0 < phnum <= 32 and phoff >= 52, "invalid program headers")
    require(shsize == 40 and 0 < shnum <= 256 and names < shnum, "invalid section headers")
    require(phoff + phsize * phnum <= len(data) and
            shoff >= 52 and shoff + shsize * shnum <= len(data), "truncated ELF tables")
    memory: dict[int, int] = {}
    ram_ranges: list[tuple[int, int]] = []
    executable_ranges: list[tuple[int, int]] = []
    stack_ranges: list[tuple[int, int]] = []
    loads = []
    for index in range(phnum):
        typ, offset, vaddr, paddr, filesz, memsz, perm, align = struct.unpack_from(
            "<IIIIIIII", data, phoff + index * phsize)
        if typ != 1:
            continue
        loads.append((offset, vaddr, paddr, filesz))
        require(filesz <= memsz and offset + filesz <= len(data), "truncated LOAD")
        require(align in (0, 1) or align & (align - 1) == 0, "LOAD alignment")
        if ram_only:
            require(load_start <= vaddr < load_end and vaddr + memsz <= load_end and
                    paddr == vaddr, "SRAM-only LOAD range or physical/virtual alias")
            if perm & 1:
                executable_ranges.append((vaddr, vaddr + filesz))
            elif perm & 2:
                stack_ranges.append((vaddr, vaddr + memsz))
        if memsz:
            require((0 <= vaddr < vaddr + memsz <= FLASH_END) or
                    (RAM_START <= vaddr < vaddr + memsz <= RAM_END), "LOAD virtual range")
        if RAM_START <= vaddr < RAM_END:
            require(ram_only or perm & 1 == 0, "executable SRAM is outside this helper profile")
            ram_ranges.append((vaddr, vaddr + memsz))
        if filesz:
            require(load_start <= paddr < paddr + filesz <= load_end,
                    "ELF load outside selected image profile")
            for i, value in enumerate(data[offset:offset + filesz], paddr):
                require(i not in memory, "overlapping LOAD")
                memory[i] = value
    require(bool(memory) and all(i in memory for i in range(load_start, load_start + 8)),
            "missing vector table")
    require((entry & ~1) in memory, "entry not loaded")
    sp = int.from_bytes(bytes(memory[i] for i in range(load_start, load_start + 4)), "little")
    reset = int.from_bytes(bytes(memory[i] for i in range(load_start + 4, load_start + 8)), "little")
    stack_end = load_end if ram_only else RAM_END
    require(RAM_START < sp <= stack_end and sp % 8 == 0 and reset == entry, "invalid reset vectors")
    if ram_only:
        require(any(a < sp <= b for a, b in stack_ranges),
                "SRAM stack missing from writable nonexecutable LOAD")
        for previous, current in zip(sorted(ram_ranges), sorted(ram_ranges)[1:]):
            require(previous[1] <= current[0], "overlapping SRAM LOAD extents")
        require(any(a <= (entry & ~1) < b for a, b in executable_ranges),
                "SRAM entry missing from executable LOAD")
    headers = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shsize) for i in range(shnum)]
    string_header = headers[names]
    require(string_header[1] == 3 and string_header[4] + string_header[5] <= len(data),
            "invalid section string table")
    strings = data[string_header[4]:string_header[4] + string_header[5]]
    allocated, sections, allocated_ranges = 0, [], []
    section_memory: dict[int, int] = {}
    executable_entry = False
    for h in headers:
        name, typ, perm, address, offset, size = h[:6]
        require(name < len(strings), "invalid section name")
        end = strings.find(b"\0", name)
        require(end >= 0, "unterminated section name")
        label = strings[name:end].decode("ascii", "strict")
        if typ != 8:
            require(offset + size <= len(data), "truncated section")
        if perm & 2 and size:
            if ram_only:
                require(load_start <= address < address + size <= load_end,
                        "allocated section outside SRAM-only profile")
                require(not perm & 4 or
                        any(a <= address < address + size <= b for a, b in executable_ranges),
                        "executable SRAM section missing from executable LOAD")
            require((0 <= address < address + size <= FLASH_END) or
                    (RAM_START <= address < address + size <= RAM_END), "allocated section range")
            if RAM_START <= address < RAM_END:
                require(ram_only or perm & 4 == 0, "executable RAM section")
                require(any(a <= address < address + size <= b for a, b in ram_ranges),
                        "RAM section missing from LOAD")
                allocated += size
                allocated_ranges.append((address, address + size))
            if perm & 4 and address <= (entry & ~1) < address + size:
                executable_entry = True
            if typ != 8:
                candidates = [(off, va, pa, count) for off, va, pa, count in loads
                              if va <= address < address + size <= va + count
                              and offset == off + address - va]
                require(len(candidates) == 1, "file-backed section missing/ambiguous LOAD")
                _, va, pa, _ = candidates[0]
                for i, value in enumerate(data[offset:offset + size], pa + address - va):
                    require(i not in section_memory, "overlapping file-backed sections")
                    section_memory[i] = value
            sections.append({"name": label, "address": address, "size": size, "type": typ})
    for previous, current in zip(sorted(allocated_ranges), sorted(allocated_ranges)[1:]):
        require(previous[1] <= current[0], "overlapping allocated SRAM")
    extent = max((end - RAM_START for _, end in ram_ranges), default=0)
    require(0 < extent <= ram_limit and allocated <= extent, "selected SRAM budget exceeded")
    image_end = max(memory) + 1
    flash_extent = 0 if ram_only else image_end
    require(executable_entry, "entry outside an executable section")
    require(all(value == 0 for address, value in memory.items() if address not in section_memory),
            "nonzero hidden LOAD padding")
    # Upstream's actual objcopy uses --gap-fill 0xff. Only proven alignment
    # holes, never section contents, are filled this way.
    expected_hex = {i: section_memory.get(i, 0xFF) for i in range(load_start, image_end)}
    return Image(expected_hex, entry, flash_extent, allocated, extent, sections,
                 load_start, image_end - load_start)


def compare(elf_bytes: bytes, hex_text: str, *, ram_only: bool = False) -> Image:
    image = elf(elf_bytes, ram_only=ram_only)
    memory, entry = ihex(hex_text, ram_only=ram_only)
    require(memory == image.memory, "ELF/HEX data or load ranges differ")
    require(entry is None or entry == image.entry, "HEX/ELF entry mismatch")
    return image


def function_body(text: str, name: str) -> str:
    matches = list(re.finditer(r"\b" + re.escape(name) + r"\(void\)\s*\{", text))
    require(len(matches) == 1, f"expected one preprocessed {name} definition")
    start, pos, depth = matches[0].start(), matches[0].end(), 1
    while depth and pos < len(text):
        depth += (text[pos] == "{") - (text[pos] == "}")
        pos += 1
    require(depth == 0, "unterminated function")
    return text[start:pos]


def startup(macros: str, preprocessed: str) -> dict[str, str]:
    definitions = dict(re.findall(r"^#define (\w+)(?: (.*))?$", macros, re.MULTILINE))
    for name in ("CONFIG_GPIO_AS_PINRESET", "CONFIG_NFCT_PINS_AS_GPIOS",
                 "CONFIG_NRF_APPROTECT_LOCK", "ENABLE_APPROTECT"):
        require(name not in definitions, f"{name} must be UNDEFINED, not zero")
    require(definitions.get("NRF52840_XXAA") == "1" and
            definitions.get("CONFIG_NRF_APPROTECT_USE_UICR") == "1", "wrong startup selection")
    require(not any("DEVELOP_IN" in key for key in definitions), "emulation startup")
    result = {}
    for name, expected in STARTUP_HASHES.items():
        body = function_body(preprocessed, name)
        result[name] = digest(re.sub(r"\s+", "", body).encode())
        require(result[name] == expected, f"unreviewed preprocessed {name} body")
    return result


def configuration(text: str, dts: str, *, ram_only: bool = False) -> None:
    _load_bounds(ram_only)
    values = dict(re.findall(r"^(CONFIG_\w+)=(.*)$", text, re.MULTILINE))
    enabled = ("NRF_802154_RADIO_DRIVER", "NRF_802154_SOURCE_NRFXLIB",
               "NRF_802154_SL_OPENSOURCE", "ENTROPY_NRF5_RNG",
               "UART_INTERRUPT_DRIVEN", "UART_0_INTERRUPT_DRIVEN", "UART_NRFX_UARTE",
               "NRF_APPROTECT_USE_UICR", "ASSERT")
    disabled = ("MPSL", "BT", "NETWORKING", "IEEE802154", "FLASH", "NRFX_NVMC",
                "NVS", "SETTINGS", "BOOTLOADER_MCUBOOT", "SECURE_BOOT", "BUILD_S1_VARIANT",
                "BOOTLOADER_PROVISION_HEX", "NRF_REGTOOL_GENERATE_UICR",
                "NFCT_PINS_AS_GPIOS", "NRF_APPROTECT_LOCK", "ZERO_LATENCY_IRQS",
                "RESET_ON_FATAL_ERROR", "REBOOT", "HW_CC3XX", "NRF_CC3XX_PLATFORM",
                "ENTROPY_CC3XX", "NRF_SECURITY", "NRF_802154_SERIALIZATION",
                "NRF_802154_ENCRYPTION", "NRF_802154_CARRIER_FUNCTIONS",
                "NRF_802154_MULTIPROTOCOL_SUPPORT", "USB_DEVICE_STACK", "SHELL",
                "NRF_802154_SOURCE_HAL_NORDIC")
    for name in enabled:
        require(values.get("CONFIG_" + name) == "y", f"{name} not selected")
    for name in disabled:
        require(values.get("CONFIG_" + name) not in ("y", "m"), f"forbidden {name}")
    for name, value in (("HEAP_MEM_POOL_SIZE", "0"), ("NRF_802154_RX_BUFFERS", "4"),
                        ("FLASH_SIZE", "0" if ram_only else "1024"), ("SRAM_SIZE", "256")):
        require(values.get("CONFIG_" + name) == value, f"wrong {name}")
    require(values.get("CONFIG_BOARD") == '"nrf52840dk_nrf52840"', "wrong board")
    require("gpio-as-nreset" not in dts and "nfct-pins-as-gpios" not in dts,
            "UICR properties still present")
    require(re.search(r"zephyr,entropy\s*=\s*&rng;", dts) is not None, "wrong entropy device")
    if ram_only:
        for name in ("XIP", "BOARD_ENABLE_DCDC", "BOARD_ENABLE_DCDC_HV",
                     "NRF_ENABLE_ICACHE"):
            require(f"# CONFIG_{name} is not set" in text.splitlines(),
                    f"SRAM-only profile must explicitly disable {name}")
        for name in ("XIP", "BOARD_ENABLE_DCDC", "BOARD_ENABLE_DCDC_HV",
                     "SOC_DCDC_NRF52X", "SOC_DCDC_NRF52X_HV", "NRF_ENABLE_ICACHE"):
            require("CONFIG_" + name not in values, f"contradictory SRAM-only {name}")
        for name, value in (("FLASH_BASE_ADDRESS", "0x0"), ("FLASH_LOAD_OFFSET", "0"),
                            ("FLASH_LOAD_SIZE", "0"), ("SRAM_BASE_ADDRESS", "0x20000000"),
                            ("SRAM_OFFSET", "0"), ("INIT_ARCH_HW_AT_BOOT", "y"),
                            ("CPU_CORTEX_M_HAS_VTOR", "y"), ("ARM_MPU", "y"),
                            ("MPU_STACK_GUARD", "y")):
            require(values.get("CONFIG_" + name) == value, f"wrong SRAM-only {name}")
    else:
        require(values.get("CONFIG_XIP") == "y", "flash profile requires XIP")
