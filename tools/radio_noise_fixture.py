# SPDX-License-Identifier: BSD-3-Clause
"""Strict offline board-image proof and PRIVATE raw snapshot decoders. No USB."""
import hashlib
import json
from pathlib import Path
import re

from clock_fixture import LENGTHS as CLOCK_LENGTHS
from prng_fixture import PRNG_LENGTHS
from radio_fifo_fixture import instructions
from verify_firmware import (
    BOARDS, cdb_address, code_bytes, peripheral_accesses, require, verify_timebase_reader,
)

SIZE = 20
COMMAND_SIZE = 16
CAPTURE_SIZE = 171
HEALTH_SIZE = 15
CLOCK_SIZE = 19
CHECKPOINTS = tuple("_radio_noise_fixture_" + name for name in ("wait", "end", "fault"))
ARM = bytes.fromhex("a6591ae500040100a08601102767983c")
RUN = bytes.fromhex("59a61ae500040100a0860110276798c3")
REQUEST = bytes.fromhex("a08601000004010010271a")
CAPTURE_SIZES = (4,) * 6 + (2,) * 3 + (1,) * 13
HEALTH_SIZES = (2,) * 6 + (1,) * 3
CLOCK_SIZES = (4, 2, 1, 4, 2, 1, 1, 1, 1, 1, 1)
OBJECTS = {"state": (155, SIZE), "command": (175, COMMAND_SIZE), "request": (191, 11),
           "capture": (202, CAPTURE_SIZE), "health": (373, HEALTH_SIZE),
           "clock": (388, CLOCK_SIZE), "initialized": (407, 1)}
HASHES = {
    "generic": (9426, "43e4e89625e5b421dc39e98d406877c9f6fa294cdf6aa54c6aa38999408791a6",
                "2bc64f5f558e88cf55adbbb27b69810ad33bdc85ec6ef6e632d9683e3766ebf8",
                "b93e53bbc2a23573b8b265d13f16c023645a13394bfc1d614eef255bf634e0e2"),
    "lg_esl29_rev03": (9466, "c7d9ba504648c166a1368a3dc9500038c05169c9d29dcfcab51a57fa3b0ad9a7",
                      "63d8ce4c94ef62eb4d7b2571d627fe24e9c4fbeac1ff6cedd1bfe553500aece7",
                      "fb8137bd0442bc40936a06d8a54b21f5cff343107ee5075bba827a4a48351022"),
}
# Entire immediate .rst snapshots, not only public labels or selected opcodes.
LISTINGS = {
    "generic": (
        "35be0c94510bfd60c8342488737b6ed6f765e6aef08c4cca1a0ad715e472c323",
        "1e14e50751eaac4f70d0a93b9fca8eb6722241e8dbe793d0e6af3262c4242301",
        "19756d3c900135df2bc3ca0c728938bee429db50571736b913d86bef39c21c19",
        "59bc4e6f07ef0427e67af15cc01520267d67a9d0efaf50462d3318ae055fc362",
        "bbdb860dc86e38b05f80628c9d8cd679bd098aee43ac3c31450fad58a8eaf8bb",
        "360f067d8727e8582f51336c6a939159ef83c81bffeec1b69b74ab59c41c72da",
        "1dec5aff5a73f142e0022ea1085b6a65d5b6ede60dba02b7e50ab6ef5ad4c64b",
        "13cdbb5977272c4ef6008cb7906fa2ced8bd1c1eb43338d9bc066d68c9748435",
        "cf0616ec8d44b1a1d30c7ea0f7f7107ceb54c7b2cb4c8e1fe3acad038d455b0f"),
    "lg_esl29_rev03": (
        "8457199b8f6bb7e8dba39ca3bdf02a380fabb534a1827b7cb2bee8e3139be677",
        "66c6139ce68b3915f2df287e897db2f53426d7b0725ab9aefee55cb4240dd216",
        "2a161e2617f26319d3bb6c618f7638072055badaf3983b546d7656451cc17141",
        "4715cdfdcd302bba8f3d679786cfe43269fbd2ad415bdfad4f23709fcee3e1d7",
        "e7ddc9a5c485cd1c8e851494f1357e937556ac7a1a772771bcab1b2d1e11a9d5",
        "23283f1293b0546f0edd790bf80d6552be3fadb5b3a35a448e5a8fc03698858d",
        "5573c590d279ca466f3462ab03c8d08e62c94123cb6e3161ce259c89e6733eb0",
        "f81f6cf119ed77af29594de88b4908431b95d603804856f5b4b4167b1ac38c45",
        "b0938a64071099001a3a00ed66f4ecc65206b1cbbb801c89567ed9352c97c1f9"),
}
LENGTHS = CLOCK_LENGTHS | PRNG_LENGTHS | {
    op: 1 for op in (0xa4, 0x5b, 0x23, 0x49, 0x2b, 0x3c, 0xc8, 0x68, 0x5d, 0x59, 0x1b, 0x1c, 0xca, 0x6a, 0x13)
}


def decode(raw):
    """Decode only a quiescent WAIT/END/FAULT record, never a running capture."""
    require(type(raw) is bytes and len(raw) == SIZE and raw[:6] == b"M2RN\x01\x14"
            and raw[14:] == b"\x1a\x01\0\0\x69\x96", "IRND fixture ABI/profile/guards changed")
    phase, reason, attempts, result, health, clock = raw[6:12]
    first = int.from_bytes(raw[12:14], "little")
    require(1 <= phase <= 4 and reason <= 4 and (phase == 4) == bool(reason) and
            attempts <= 1 and result in (*range(14), 255) and
            health in (*range(5), 255) and clock <= 9 and first <= 1024,
            "IRND fixture phase/results/bounds changed")
    if phase in (1, 2):
        require(not attempts and result == health == 255 and not first and clock == (8 if phase == 1 else 0),
                "IRND WAIT is not disarmed/admitted")
    if phase == 3:
        require(attempts == 1 and result == clock == 0 and health in (0, 3, 4),
                "IRND END lacks acquisition success")
    if phase == 4:
        if reason == 1:
            require(not attempts and result == health == 255 and not first and clock in (0, 8),
                    "IRND packet fault changed")
        if reason == 3:
            require(not attempts and result == health == 255 and not first and clock not in (0, 8),
                    "IRND clock fault changed")
        if reason == 4:
            require(attempts == 1 and result in range(1, 14) and clock == 0 and health in range(5),
                    "IRND acquisition fault changed")
    if reason != 2:
        require(bool(first) == (health in (3, 4)), "IRND first health failure changed")
    return dict(phase=("DISARMED", "ADMITTED", "END", "FAULT")[phase-1], attempts=attempts,
                result=result, health_result=health, first_failure=first, clock_result=clock,
                reason=reason, fault=(None, "PACKET", "INVARIANT", "CLOCK", "ACQUISITION")[reason])


def decode_capture(raw):
    """Contains PRIVATE raw data_hex. Never print/publish a real capture in CI."""
    require(type(raw) is bytes and len(raw) == CAPTURE_SIZE, "IRND capture size changed")
    names = ("elapsed_ticks", "first_before", "last_after", "min_gap", "max_gap", "max_span",
             "samples", "timed_samples", "polls", "phase", "writes", "verified", "actions",
             "timebase_status", "rx_enable", "calibration", "signals", "rssi_valid",
             "errors", "flags0", "flags1", "last_raw")
    result = {}; offset = 0
    for name, size in zip(names, CAPTURE_SIZES):
        result[name] = int.from_bytes(raw[offset:offset+size], "little"); offset += size
    count = result["samples"]
    require(0 <= result["timed_samples"] <= count <= 1024 and result["polls"] <= 10000 and
            result["phase"] <= 5 and 0 <= result["verified"] <= result["writes"] <= 10 and
            result["actions"] in (0, 1, 3), "IRND capture bounds changed")
    require(all(result[name] <= 0xffffff for name in names[:6]), "IRND raw tick range changed")
    data = raw[offset:]
    require(all(not ((data[i >> 3] >> (i & 7)) & 1) for i in range(count, 1024)),
            "IRND unused raw bits are not zero")
    if result["phase"] == 5:
        require(count == result["timed_samples"] == 1024 and result["actions"] == 3 and
                result["writes"] == result["verified"] == 10, "IRND completed capture mismatch")
    result["data_hex"] = data.hex()
    return result


def verify_code(image, board):
    require(board in HASHES, "IRND fixture requires an exact board")
    size, digest, _, _ = HASHES[board]
    require(size <= 16384 and hashlib.sha256(code_bytes(image, size)).hexdigest() == digest,
            "IRND board complete CODE identity changed")


def verify_fixture(image, symbols, debug, board):
    verify_code(image, board)
    # Callers must decode raw bytes, without universal-newline translation or
    # split/join normalization. Non-ASCII separators cannot have this identity.
    require(debug.isascii() and hashlib.sha256(debug.encode("ascii")).hexdigest() == HASHES[board][2],
            "IRND board complete CDB ABI identity changed")
    require(hashlib.sha256(json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode("ascii")).hexdigest()
            == HASHES[board][3], "IRND board complete map identity changed")
    require(symbols["l_XSEG"] + 64 <= 768 and symbols["s_SSEG"] == 0x2f and symbols["l_SSEG"] == 209,
            "IRND board memory budget changed")
    wait = 0x1eb0 + 40 * BOARDS[board]
    require([symbols[n] for n in CHECKPOINTS] == [wait, wait+2, wait+5] and
            bytes(image[a] for a in range(wait, wait+8)) == b"\0\x22\0\x80\xfd\0\x80\xfd",
            "IRND board checkpoint NOP/RET/terminal loops changed")
    for name, (address, size) in OBJECTS.items():
        key = "_radio_noise_fixture_" + name
        require(symbols[key] == address and cdb_address(debug, f"L:G${key[1:]}$0_0$0") == address,
                "IRND board caller symbol mismatch")
        declarations = re.findall(rf"^S:G\${key[1:]}\$[^(]+\(\{{(\d+)\}}", debug, re.M)
        require(declarations and all(int(n) == size for n in declarations), "IRND raw struct ABI changed")
    require((symbols["_radio_noise_reserved_end"], symbols["___memcpy_PARM_2"],
             symbols["_memset_PARM_2"], symbols["__gptrput_PARM_2"], symbols["l_XSEG"]) ==
            (154, 428, 436, 439, 440), "IRND entire private/caller/libc boundary changed")
    for name, packet in (("arm", ARM), ("run", RUN)):
        start = symbols["_radio_noise_fixture_" + name]
        require(bytes(image[start+i] for i in range(16)) == packet, "IRND exact CODE packet changed")
    verify_timebase_reader(image, symbols, debug, OBJECTS["state"][0], SIZE)
    # This new composition has its own complete CODE/CDB/listing binding; do
    # not expand any older standalone clock proof's accepted relocation set.
    clock = instructions(image, cdb_address(debug, "L:Fclock$effective_status$0$0"),
                         symbols["_noise_health_start"], LENGTHS)
    caller = instructions(image, symbols["_radio_noise_fixture_poll"],
                          cdb_address(debug, "L:XG$radio_noise_fixture_poll$0$0")+1, LENGTHS)
    sites = {}
    for code in (clock, caller):
        for pc, raw, reg in peripheral_accesses(code):
            write = raw[0] == 0x75 or 0x88 <= raw[0] <= 0x8f
            observed = reg if write or raw[0] == 0xb5 else raw[0]-0xa8 if 0xa8 <= raw[0] <= 0xaf else 0xe0
            sites[pc] = ("w" if write else "c", reg, observed)
    # Same unchanged collector, now with the clock/health prefix and real board
    # caller. These projected sites observe real operands, never patch returns.
    sites.update({0x10ba: ("o", 0x624a, 0xe0), 0x1a47: ("r", 0x61a7, 0xe0),
                  0x17e7: ("w", None, None), 0x188e: ("w", 0xe1, 0xe1),
                  0x1d5c: ("w", 0x618d, 0x618d),
                  symbols["_timebase_read_awake_ticks24"] + 3: ("t", 0x95, 0xe0)})
    for pc, (kind, address, _) in sites.items():
        if address is None or address >= 256:
            require(image[pc] == (0xf0 if kind == "w" else 0xe0), "IRND projected MOVX site changed")
    return dict(checkpoints=[wait, wait+2, wait+5], sites=sites, stack_start=0x2f,
                **{name: address for name, (address, _) in OBJECTS.items()})


def modules(board):
    return ("timebase", "clock", "noise_health", "radio_noise", "startup", "status", board,
            "radio_noise_fixture", "radio_noise_fixture_state")


def verify_listings(output, image, symbols, board):
    private, caller = set(), set()
    for name, digest in zip(modules(board), LISTINGS[board]):
        raw = (Path(output) / f"radio_noise_fixture.{name}.rst").read_bytes()
        require(hashlib.sha256(raw).hexdigest() == digest, "IRND complete relocated listing changed: " + name)
        text = raw.decode("ascii")
        listed = [(int(m[1], 16), bytes.fromhex(m[2])) for m in re.finditer(
            r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", text, re.M)]
        require(all(bytes(image.get(a+i, 255) for i in range(len(code))) == code for a, code in listed),
                "IRND listing differs from actual CODE")
        if name in ("timebase", "clock", "noise_health", "radio_noise", "radio_noise_fixture_state"):
            segment = text.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
            target = caller if name == "radio_noise_fixture_state" else private
            for a, n in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
                region = set(range(int(a, 16), int(a, 16)+int(n)))
                require(region and not region & (private | caller), "IRND allocation overlap")
                target.update(region)
    require(private == set(range(155)) and caller == set(range(155, 428)) and symbols["l_XSEG"] == 440,
            "IRND entire service/compiler/caller/libc ownership changed")


def verify_memory(memory):
    require("ERROR" not in memory.upper() and
            "Stack starts at: 0x2f (sp set to 0x2e) with 209 bytes available" in memory and
            "EXTERNAL RAM     0x0000   0x01b7     440" in memory,
            "IRND linker accounting/error changed")


def load_image(output, board):
    from debug_image import DebugImage
    return DebugImage(Path(output), board, "radio_noise_fixture")
