# SPDX-License-Identifier: BSD-3-Clause
"""Offline-only flash fixture ABI and genuine linked CODE/ABI/allocation proof.

No debugger import, device access, private backup reader or destructive runner.
"""
import hashlib
import re

from prng_fixture import PRNG_LENGTHS
from radio_fifo_fixture import instructions
from verify_firmware import cdb_address, code_bytes, peripheral_accesses, require

LENGTHS = PRNG_LENGTHS | {0xb4: 3, 0x49: 1, 0x73: 1, 0x23: 1, 0x59: 1, 0xce: 1,
                         0x13: 1, 0xcb: 1, 0xc8: 1, 0x68: 1, 0x5a: 1}
HASHES = {
    0xd1d: (4168, "56206c0393eb59b11676b870a15979b87bb8614b64afd75f789de1d3181d7a9a"),
    0xd45: (4208, "b32325c16f63cc571078289b59c4cc081535b2e12fb981cbd5dc666824b70707"),
}
# Identical CODE, DATA/BIT and XDATA addresses to the published #7 composition.
MODULES = {
    "exec": (0x62, 0x4fa, "9391bf2c3ed0f928b81f92efe46e22db36615702ac3bae8b5ec7ef2fc422401b"),
    "reader": (0x4fa, 0x7f8, "12faaebc8614269a01d131bf57909167756ccf39cfd9bab2a99d8a687fcacd4a"),
    "service": (0x7f8, 0xc3f, "1da0b78277caf140e3ef20fa0cf7f06f36289dfc217b9a6d68cb10caf7b463a5"),
}
FIELDS = (("signature", 4), ("version", 1), ("size", 1), ("phase", 1), ("reason", 1),
          ("page", 1), ("step", 1), ("result", 1), ("checks", 1), ("remaining_low", 1),
          ("remaining_high", 1), ("guards", 2))


def packet(stage, page):
    """Pure bytes; never an authorization or a writer. Reject before narrowing."""
    require(stage in ("arm", "run") and type(page) is int and 0 <= page < 2,
            "Invalid flash handshake stage/page")
    opcode, token = (0xa6, 0x3c) if stage == "arm" else (0x59, 0xc3)
    return bytes((opcode, opcode ^ 255, page, page ^ 255, token, token ^ 255, 0x69, 0x96))


def decode(data):
    require(isinstance(data, bytes) and len(data) == 16 and data[:6] == b"M2FL\x01\x10"
            and data[14:] == b"\x69\x96", "Flash fixture ABI signature/version/size/guards")
    phase, reason, page, step, result, checks = data[6:12]
    remaining = int.from_bytes(data[12:14], "little")
    require(1 <= phase <= 5 and 0 <= reason <= 4 and (phase == 5) == bool(reason) and
            page in (0, 1, 255) and step <= 5 and result in (*range(11), 255) and
            checks <= 3 and remaining <= 256, "Flash fixture ABI bounds/phase")
    if phase in (1, 2):
        require(not step and not checks and result == 255 and remaining > 0 and
                (page == 255 if phase == 1 else page < 2), "Flash fixture arming record")
    if phase in (3, 4):
        require(page < 2 and not remaining, "Flash fixture command scope")
    if phase == 3:
        require(step < 5 and checks == (0 if step == 0 else 1 if step <= 3 else 3) and
                result in (10, (255, 4, 0, 0, 5)[step]), "Flash fixture running progress")
    if phase == 4:
        require(step == 5 and result == 0 and checks == 3, "Flash fixture END lacks verified sequence")
    if phase == 5:
        require(step < 5, "Flash fixture fault after END")
        if reason <= 2:
            require(step == checks == 0 and result == 255 and
                    (remaining == 0 if reason == 1 else remaining > 0), "Flash fixture admission fault")
        else:
            require(page < 2 and remaining == 0 and
                    (step in (0, 3) if reason == 3 else step in (1, 2, 4)),
                    "Flash fixture service/history fault")
    return dict(phase=phase, reason=reason, page=page, step=step, result=result,
                checks=checks, remaining=remaining)


def verify_fixture(image, symbols, debug):
    wait = symbols.get("_flash_fixture_wait")
    require(wait in HASHES, "Missing/unknown flash fixture checkpoint symbol")
    size, digest = HASHES[wait]
    require(hashlib.sha256(code_bytes(image, size)).hexdigest() == digest,
            "Flash fixture complete CODE changed")
    require(bytes(image[a] for a in range(wait, wait+8)) == b"\0\x22\0\x80\xfd\0\x80\xfd",
            "Flash fixture checkpoints changed")
    expected = {
        "_flash_fixture_wait": wait, "_flash_fixture_end": wait+2, "_flash_fixture_fault": wait+5,
        "_main": wait+8, "_flash_fixture_initialize": wait+85, "_flash_fixture_poll": wait+218,
        "_flash_exec_template_end": 0xdd, "_flash_exec_command": 0x1cf, "_flash_nv_read": 0x5e0,
        "_flash_nv_erase": 0xbc6, "_flash_nv_program": 0xbf7, "_flash_write_diagnostic": 0xc3b,
        "_flash_exec_command_PARM_2": 0x91, "_flash_exec_command_PARM_3": 0x92,
        "_flash_exec_command_PARM_4": 0x94, "_flash_exec_command_PARM_5": 0x96,
        "_flash_nv_read_PARM_2": 0xc9, "_flash_nv_read_PARM_3": 0xcb, "_flash_nv_read_PARM_4": 0xcd,
        "_flash_nv_erase_PARM_2": 0x189, "_flash_nv_program_PARM_2": 0x18c,
        "_flash_nv_program_PARM_3": 0x18e, "_flash_nv_program_PARM_4": 0x190,
        "s_XSEG": 0, "l_XSEG": 436, "s_SSEG": 0x21, "l_BSEG": 1,
        "l_PSEG": 0, "l_XISEG": 0, "l_XABS": 0,
    }
    objects = {
        "flash_exec_work": (0, 9), "flash_exec_ram": (9, 123), "flash_exec_reserved_end": (0x9a, 1),
        "flash_fault": (0x9b, 1), "flash_reserved_end": (0xd0, 1), "flash_write_status": (0xd1, 8),
        "flash_write_known": (0xd9, 1), "flash_write_used": (0xda, 128), "flash_write_word": (0x15a, 4),
        "flash_write_check": (0x15e, 32), "flash_write_reserved_end": (0x193, 1),
        "flash_fixture_state": (0x194, 16), "flash_fixture_mailbox": (0x1a4, 8), "flash_fixture_word": (0x1ac, 4),
    }
    for name, value in expected.items():
        require(symbols.get(name) == value, "Flash fixture symbol/allocation changed: "+name)
    for name, (address, length) in objects.items():
        require(symbols.get("_"+name) == cdb_address(debug, f"L:G${name}$0_0$0") == address,
                "Flash fixture object symbol/address changed")
        sizes = re.findall(rf"^S:G\${name}\$[^(\n]+\(\{{(\d+)\}}", debug, re.M)
        require(sizes and all(int(n) == length for n in sizes), "Flash fixture object ABI size changed")
    for module in ("flash_fixture", "flash_fixture_state"):
        records = re.findall(rf"^T:F{module}\$[^\[]+\[(.*)\]$", debug, re.M)
        layouts = [re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", r) for r in records]
        offset = 0; fields = []
        for name, length in FIELDS:
            fields.append((str(offset), name, str(length))); offset += length
        require(fields in layouts, "Flash fixture field ABI changed")
    private = "\n".join(sorted(set(line for line in debug.splitlines()
        if re.match(r"[SLT]:(?:Lflash(?:_exec|_write)?\.|Fflash(?:_exec|_write)?\$)", line))))
    require(hashlib.sha256(private.encode()).hexdigest() ==
            "76a9f3c74c67db1991888c47275f21f0e1876e9a236d89f55f7a65c919725275",
            "Flash service complete private CODE/DATA/XDATA/BIT/ABI records changed")
    for name in ("flash_nv_erase", "flash_nv_program"):
        require(f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0" in debug, "Flash result ABI changed")
    require("F:G$flash_write_diagnostic$0_0$0({2}DF,DX," in debug, "Flash diagnostic pointer ABI changed")
    for name, (lo, hi, digest) in MODULES.items():
        require(hashlib.sha256(bytes(image[a] for a in range(lo, hi))).hexdigest() == digest,
                "Flash fixture replaced the genuine service: "+name)
        instructions(image, lo, hi, LENGTHS)
    body = instructions(image, cdb_address(debug, "L:Fflash_fixture_state$budget$0$0"), size-2, LENGTHS)
    require(not peripheral_accesses(body) and
            all(int.from_bytes(raw[1:], "big") < 0x1e00 for raw in body.values() if raw[0] == 0x90),
            "Flash fixture bypasses services with MMIO")
    require(not any(name.startswith(("__gptr", "_flash_exec_host", "_host_flash")) for name in symbols),
            "Flash fixture acquired generic helpers/native model")
    return dict(wait=wait, end=wait+2, fault=wait+5, state=0x194, mailbox=0x1a4, word=0x1ac)


def verify_listings(output, image):
    """Per-image relocated snapshots, never the mutable shared module .rst files."""
    covered = set()
    for name, (lo, hi, _) in MODULES.items():
        listing = (output/f"flash_fixture.{name}.rst").read_text()
        actual = [(int(m[1], 16), bytes.fromhex(m[2])) for m in re.finditer(
            r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", listing, re.M)]
        require(list(instructions(image, lo, hi, LENGTHS).items()) == actual,
                "Flash fixture snapshot instruction mismatch")
        segment = listing.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        for address, size in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
            region = set(range(int(address, 16), int(address, 16)+int(size)))
            require(region and not region & covered, "Flash private prefix overlap")
            covered |= region
    require(covered == set(range(0x194)), "Flash fixture entire private prefix escaped fence")
    listing = (output/"flash_fixture.state.rst").read_text()
    actual = [(int(m[1], 16), bytes.fromhex(m[2])) for m in re.finditer(
        r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", listing, re.M)]
    size = len(image)
    wait = next(wait for wait, (length, _) in HASHES.items() if length == size)
    require(actual == list(instructions(image, wait+42, size-2, LENGTHS).items()),
            "Flash fixture state snapshot instruction mismatch")
    segment = listing.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
    state_bytes = set()
    for address, length in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
        region = set(range(int(address, 16), int(address, 16)+int(length)))
        require(region and not region & (covered | state_bytes), "Flash fixture caller allocation overlap")
        state_bytes |= region
    require(state_bytes == set(range(0x194, 436)), "Flash fixture caller/scratch allocation escaped budget")
