# SPDX-License-Identifier: BSD-3-Clause
"""Offline exact board-image/TX fixture proof and decoder; never opens hardware."""
import hashlib
from pathlib import Path
import re

from clock_fixture import LENGTHS as CLOCK_LENGTHS, require_cdb_lf, verify_clock_code
from prng_fixture import PRNG_LENGTHS
from radio_fifo_fixture import FIFO_LENGTHS, instructions, verify_fifo_relocated
from verify_firmware import (
    BOARDS, cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require,
    verify_timebase_reader,
)

SIZE = 24
BODY = bytes.fromhex("41 88 5a ff ff ff ff 34 12 54 58 46 31")
CHECKPOINTS = tuple("_radio_tx_fixture_"+n for n in ("wait", "end", "fault"))
LENGTHS = CLOCK_LENGTHS | PRNG_LENGTHS | FIFO_LENGTHS | {
    0x1c: 1, 0x23: 1, 0x2b: 1, 0x3c: 1, 0x49: 1, 0x52: 2, 0x6f: 1, 0xa4: 1,
}
HASHES = {
    "generic": (11291, "c75716d32d7575d005c1175a224f5a16d42eae3ddd4ad261ec3a11251bc0ac08",
                "29dd66f408a74216fd5c1acc54e5c6a3a874c898306054fbeaa51d04224293a7",
                "9c4dc40b05566c57275e4459c07df30cb4a4f4d0e065eaf2bf59af757b2e574f"),
    "lg_esl29_rev03": (11331, "e47714206c44cc4be5937da4752a1baa637e86b48fefab46390313c04c4c4763",
                      "ffb050b314786520e480caebd19f34370deec97e4b9bd554a3de2bff0ae71806",
                      "ecc7df049d46af726c57d3698c9779d084ebec4669740c9d0386da36018c4a1c"),
}
OBJECTS = {"state": (227, 24), "mailbox": (251, 8), "clock": (259, 19),
           "fifo": (278, 21), "tx": (299, 29), "initialized": (328, 1)}
SETTINGS = (0x618a, 0x6180, 0x6182, 0x61b2, 0x61fa, 0x61ae, 0x618f, 0x6190, 0x6196, 0x6197)
CLOCK_SIZES = (4, 2, 1, 4, 2, 1, 1, 1, 1, 1, 1)
FIFO_SIZES = (4, 2)+(1,)*15
TX_SIZES = (4, 2)+(1,)*23
LISTINGS = {
    "generic": (
        "6e406ee057958d1b7d5ea65a99eec82c31cbddaebb4259022b15a95ec74ceca4",
        "89837149f54c26bff6b1ed3ac3571a929ef26510440c36ab619e13b618b04f6b",
        "81b838d852741da3e82019f67fd9c96cf794581317da226f03418c6d57e6b9cf",
        "a948f426929c58689345d07b209c5a860cdd6c7a3a8cd1b5d59c7972debf4882",
        "b55be16890e5cdc63d8877d1eb5a9b513a51401d38c80cd7dd0a5653d856366a",
        "0a36fe3498e6959929ae53f73d0b85a9326a1fdc20f1cbe95c9f3cb56bcad09f",
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "00f2e52508f3104df4e5468f23169c8f8cffe6a8de36842fca17ba8101f03701",
        "74c3f8df32a8fc91274c85f541900a9f4566c0f0eccf7a7e083208b8944bacdd"),
    "lg_esl29_rev03": (
        "88458e5aeafb9bd88ebe0552f386550a6e25d9c77612c5e528d969b31bbcefd3",
        "27d98aa89dc856b4892cb8c2c2cdb507eb3e6f4d47901d8678c7de4e3c10e4c1",
        "1d90a3bdbaa3efb5829f00248d2c2f24d4fe685ffee3283fb70d14cf20b469d2",
        "87dee2179734940fc436d805327216c20dd5f95fbdb61e81879aaaf3c0f124e8",
        "77fad8a3ae9808b2b50c1d8ca6066ad8cc20d4741438ae8f56d874d1fb7bba8b",
        "12f10421e847e41e65668d56ad04eac0eac2e86d418c47e6fc817a37504cabfd",
        "83439c50439d1f1e199f52e6e2a10e73b3fb732d641157b7381edeb3a8c14139",
        "16be25f3cc78cba6f464d89d532f0189726026a0a431bbbe3b427487497d1f96",
        "d18b9c64d68834b23582b01db2d5d6bbacc7a343b10b22f9a32b8304f4eccdab"),
}


def packet(stage):
    require(stage in ("arm", "run"), "TX fixture unknown admission stage")
    op, token = (0xa6, 0x3c) if stage == "arm" else (0x59, 0xc3)
    return bytes((op, op ^ 255, 15, 240, token, token ^ 255, 0x69, 0x96))


def decode(data):
    require(type(data) is bytes and len(data) == SIZE and data[:6] == b"M3TX\x01\x18" and
            data[14:17] == b"\x0f\x05\x0d" and data[19:] == b"\x69\x96\0\0\0",
            "TX fixture signature/version/size/profile/guards changed")
    phase, reason, stage, attempts, completed, clock, fifo, tx = data[6:14]
    remaining = int.from_bytes(data[17:19], "little")
    require(1 <= phase <= 6 and reason <= 8 and (phase == 6) == bool(reason) and
            stage <= 6 and attempts <= 1 and completed <= 1 and remaining <= 256 and
            clock <= 9 and fifo in (*range(13), 255) and tx in (*range(15), 255),
            "TX fixture phase/result/bounds changed")
    if phase in (1, 2, 3):
        require(not reason and not stage and not attempts and not completed and
                clock == 8 and fifo == tx == 255 and
                (remaining > 0 if phase < 3 else remaining == 0), "TX admission state changed")
    if phase == 4:
        raise ValueError("Live TX execution is not an allowed observation checkpoint")
    if phase == 5:
        require(stage == 6 and attempts == completed == 1 and clock == 0 and
                fifo in (0, 1) and tx in (0, 2) and remaining == 0, "TX END lacks verified sequence")
    if phase == 6:
        require(not completed and stage < 6, "TX fault cannot be completed")
        if reason in (1, 2):
            require(stage == attempts == 0 and clock == 8 and fifo == tx == 255 and
                    (not remaining if reason == 1 else True), "TX admission fault changed")
        elif reason >= 4:
            require(stage == reason-3 and not remaining and clock != 8 and
                    attempts == int(stage >= 4), "TX operational fault stage changed")
    return dict(phase=phase, reason=reason, stage=stage, attempts=attempts, completed=completed,
                clock_result=clock, fifo_result=fifo, tx_result=tx, remaining=remaining)


def check_end(record, clock, fifo, tx):
    require(len(clock) == 19 and len(fifo) == 21 and len(tx) == 29, "TX diagnostic size changed")
    if record["phase"] != 5:
        return
    require(clock[6] == 0 and clock[18] == 8 and clock[15:18] == b"\x88"*3,
            "TX END lacks confirmed XOSC32")
    require(fifo[6] == 0 and fifo[20] == 1 and fifo[12:19] == bytes(7) and
            not fifo[11] and not fifo[19] & 0xc0 and fifo[7] == fifo[8],
            "TX END FIFO clear not completely confirmed")
    require(tx[6:8] == b"\x07\x0a" and tx[8] == 10 and tx[10] == 1 and
            tx[13:16] == b"\x01\0\0" and tx[18] == 0 and
            not tx[19] & 0x40 and not tx[20] & 0x27,
            "TX END lacks restored idle/no-error completion")
    require(tx[12] == int(record["tx_result"] == 0), "TXDONE is not busy/delivery")


def private_records(debug):
    # Preserve the complete multiset, including F helper declarations, XF end
    # labels and all T/field records in every service/startup/board/caller unit.
    # LF alone separates raw CDB records; malformed suffixes must stay hashed.
    require_cdb_lf(debug)
    return "\n".join(sorted(l for l in debug.split("\n") if re.match(
        r"^[FSLT]:(?:X?F|L)(?:clock|timebase|radio_tx|radio_fifo|startup|status|"
        r"generic|lg_esl29_rev03|radio_tx_fixture|radio_tx_fixture_state)[.$]", l)))


def public_records(debug):
    # Public declarations/addresses can repeat identically across translation
    # units. A conflicting alternative is a distinct record and changes the
    # complete inventory digest; it cannot hide behind a valid first match.
    require_cdb_lf(debug)
    return "\n".join(sorted(set(l for l in debug.split("\n") if re.match(r"^[SFLT]:(?:G\$|XG\$)", l))))


def verify_code(image, board):
    require(board in HASHES, "TX fixture requires an exact board")
    size, digest, _, _ = HASHES[board]
    require(hashlib.sha256(code_bytes(image, size)).hexdigest() == digest,
            "TX fixture complete instructions/constants/runtime changed")


def verify_fixture(image, symbols, debug, board):
    verify_code(image, board)
    require(hashlib.sha256(private_records(debug).encode()).hexdigest() == HASHES[board][2] and
            hashlib.sha256(public_records(debug).encode()).hexdigest() == HASHES[board][3],
            "TX fixture complete public/private/caller/field ABI changed")
    delta = 40*BOARDS[board]
    wait, end, fault = 0x2725+delta, 0x2727+delta, 0x272a+delta
    for name, value in dict(zip(CHECKPOINTS, (wait, end, fault))).items():
        require(symbols.get(name) == value, "TX checkpoint symbol changed")
    require(bytes(image[a] for a in range(wait, wait+8)) == b"\0\x22\0\x80\xfd\0\x80\xfd",
            "TX WAIT/terminal loop changed")
    expected = {"_main": wait+8, "_m0_status": 0x1e00, "__XPAGE": 0x93,
                "_radio_tx_reserved_end": 172, "__gptrput_PARM_2": 346,
                "_memset_PARM_2": 343, "_memset_PARM_3": 344,
                "s_XSEG": 0, "l_XSEG": 347, "l_XISEG": 0, "l_XABS": 0, "l_PSEG": 0,
                "s_SSEG": 97, "l_SSEG": 159, "__start__stack": 97,
                "s_DSEG": 0, "l_DSEG": 126, "s_OSEG": 76, "l_OSEG": 21,
                "l_ISEG": 0, "s_BSEG_BYTES": 32, "l_BSEG_BYTES": 1, "l_BSEG": 6,
                "l_REG_BANK_0": 8, "s_REG_BANK_0": 0,
                "l_REG_BANK_1": 0, "l_REG_BANK_2": 0, "l_REG_BANK_3": 0}
    for name, value in expected.items():
        require(symbols.get(name) == value, "TX allocation/runtime boundary changed: "+name)
    for name, (address, size) in OBJECTS.items():
        key = "_radio_tx_fixture_"+name
        require(symbols.get(key) == address and cdb_address(debug, f"L:G${key[1:]}$0_0$0") == address,
                "TX caller symbol changed")
        sizes = re.findall(rf"^S:G\${key[1:]}\$[^(]+\(\{{(\d+)\}}", debug, re.M)
        require(sizes and all(int(n) == size for n in sizes), "TX caller storage ABI changed")
    require(symbols.get("_radio_tx_fixture_body") == 0x2c0e+delta and
            bytes(image[a] for a in range(0x2c0e+delta, 0x2c1b+delta)) == BODY,
            "TX synthetic CODE body changed")
    require(all(f"C${source}.c$" in debug for source in
                ("startup", "status", "timebase", "clock", "radio_fifo", "radio_tx",
                 "radio_tx_fixture", "radio_tx_fixture_state")) and f"M:{board}\n" in debug,
            "TX required real source missing")
    require(not re.search(r"C\$(?:test_|host_|radio_rx|radio_queue|mac_|flash)", debug) and
            not any(n.startswith(("_radio_tx_test", "_host_", "_radio_rx_", "_radio_queue_")) for n in symbols),
            "Standalone/model/legacy RX must never enter TX board firmware")
    fifo, _ = verify_fifo_relocated(image, symbols, debug)
    clock, _ = verify_clock_code(image, symbols, debug)
    verify_timebase_reader(image, symbols, debug, 227, SIZE)
    tx = instructions(image, 0xdd8, 0x1ef4, LENGTHS)
    require([raw.hex() for _, raw, _ in peripheral_accesses(tx)] == [
        "e5a8", "e5b8", "e59a", "acbe", "e5c6", "b59e02", "e5bf", "e5e9", "e591",
        "e5c6", "75913d", "75e1e9", "75e1e3", "75e1eb", "75e1ea",
    ], "TX exact SFR/strobe/RW0 instruction inventory changed")
    require(bytes(image[a] for a in range(0xdd8, 0xddd)) == b"\0\0\0\0\x22",
            "TX real four-clock minimum leaf changed")
    sites = {}
    for code in (fifo, tx, clock):
        for pc, raw, reg in peripheral_accesses(code):
            write = raw[0] == 0x75 or 0x88 <= raw[0] <= 0x8f
            observed = reg if write or raw[0] == 0xb5 else raw[0]-0xa8 if 0xa8 <= raw[0] <= 0xaf else 0xe0
            sites[pc] = ("w" if write else "r", reg, observed)
        for pc, raw in code.items():
            if raw[0] == 0x90 and 0x6000 <= int.from_bytes(raw[1:], "big") < 0x6400:
                a = int.from_bytes(raw[1:], "big")
                if code.get(pc+3) == b"\xe0": sites[pc+3] = ("r", a, 0xe0)
                elif code.get(pc+3) == b"\xf0": sites[pc+3] = ("w", a, a)
                else:
                    require(a == 0x618d and code.get(pc+3) == b"\x74\x80" and
                            code.get(pc+5) == b"\xf0", "TX unexpected MMIO addressing")
                    sites[pc+5] = ("w", a, a)
    sites[0xf0c] = ("r", None, 0xe0)
    sites[0x17d2] = ("w", None, None)
    for pc, reg in ((0x65, 0x95), (0x6b, 0x96), (0x71, 0x97)):
        require(bytes(image[a] for a in range(pc, pc+2)) == bytes((0xe5, reg)), "TX real timer reader changed")
        sites[pc] = ("r", reg, 0xe0)
    return dict(checkpoints=[wait, end, fault], sites=sites,
                **{name: address for name, (address, _) in OBJECTS.items()})


def verify_listings(output, image, symbols, board):
    private, caller = set(), set()
    modules = ("timebase", "radio_fifo", "radio_tx", "clock", "startup", "status", board,
               "radio_tx_fixture", "radio_tx_fixture_state")
    for name, digest in zip(modules, LISTINGS[board]):
        text = (output/f"radio_tx_fixture.{name}.rst").read_text()
        listed = [(int(m[1], 16), bytes.fromhex(m[2])) for m in re.finditer(
            r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", text, re.M)]
        require((listed or name == "generic") and all(bytes(image.get(a+i, 255) for i in range(len(raw))) == raw
                               for a, raw in listed), "TX per-image listing differs from CODE")
        packed = b"".join(a.to_bytes(2, "big")+bytes([len(raw)])+raw for a, raw in listed)
        require(f".module {name}" in text and hashlib.sha256(packed).hexdigest() == digest,
                "TX complete ordered listing inventory changed")
        if name in ("timebase", "radio_fifo", "radio_tx", "clock", "radio_tx_fixture_state"):
            segment = text.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
            target = caller if name == "radio_tx_fixture_state" else private
            for a, n in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
                region = set(range(int(a, 16), int(a, 16)+int(n)))
                require(region and not region & (private | caller), "TX allocations overlap")
                target.update(region)
        if name == "radio_tx_fixture_state":
            code = dict(listed)
            require(not peripheral_accesses(code) and
                    all(int.from_bytes(raw[1:], "big") < 0x1e00 or
                        int.from_bytes(raw[1:], "big") == symbols["_radio_tx_fixture_body"]
                        for raw in code.values() if raw[0] == 0x90),
                    "TX caller bypassed real services")
    require(private == set(range(227)) and caller == set(range(227, 343)),
            "TX complete service/caller prefix escaped its allocation")


def load_image(output, board):
    """Common canonical artifact preflight plus the nine per-image listings."""
    # DebugImage dispatches back to verify_fixture, never to this loader.
    # Keep the import lazy so either module may be imported first.
    from debug_image import DebugImage
    require(board in HASHES, "TX fixture requires an exact board")
    output = Path(output); stem = output/"radio_tx_fixture"
    checked = DebugImage(output, board, "radio_tx_fixture")
    image = parse_ihex(stem.with_suffix(".ihx").read_text())
    symbols = parse_symbols(stem.with_suffix(".map").read_text())
    verify_listings(output, image, symbols, board)
    # Retain the original emitted .mem error/summary checks as well as common
    # map/CDB/stack/alias/budget accounting. There is no parallel image type.
    memory = stem.with_suffix(".mem").read_text()
    require("Stack starts at: 0x61 (sp set to 0x60) with 159 bytes available" in memory and
            "ERROR" not in memory and "EXTERNAL RAM     0x0000   0x015a     347" in memory,
            "TX linker accounting/error changed")
    return checked
