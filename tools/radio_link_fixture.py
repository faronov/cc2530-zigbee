# SPDX-License-Identifier: BSD-3-Clause
"""Exact offline proof/decoder for the boot-disarmed same-owner TX/RX image."""
import hashlib
import json
from pathlib import Path
import re

from clock_fixture import verify_clock_code
from radio_tx_fixture import CLOCK_SIZES
from verify_firmware import (
    BOARDS, cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses,
    require, verify_timebase_reader,
)

SIZE, CHANNEL, CASES = 36, 26, 35
BODY = bytes.fromhex("41 88 5a ff ff ff ff 34 12 4c 4e 4b 31")
RADIO_SIZES = (4, 2, 2) + (1,) * 18
CHECKPOINTS = tuple("_radio_link_fixture_" + n for n in ("wait", "end", "fault"))
OBJECTS = {"state": (335, 36), "mailbox": (371, 8), "clock": (379, 19),
           "radio": (398, 26), "config": (424, 14), "frames": (438, 256),
           "body": (694, 13), "initialized": (707, 1)}
SETTINGS = (0x6180, 0x6181, 0x6182, 0x6189, 0x618a, 0x6194, 0x6195,
            0x61b2, 0x61fa, 0x61ae, 0x618f, 0x6190, 0x6191)
HASHES = {
    "generic": (10758, "67143a40a545bed11ceaf577f3045facb8aaed6eaef76c9168d58c9024be2ca4",
                "7bafa9e411d09cda5f8407eb464ac8467b325a32aeb300ce8600f8fc1bb74881",
                "b349f3037e04fcd666dff13c36f5e2b802a14214443e37531c80654468aefae2",
                "c98caf2c7ee4eb53813684e49c84ed3c5dc8de3a844b0392d556e16d454071b7"),
    "lg_esl29_rev03": (10798, "ccfad2a8149454ed7dcc225295261a8267171cf62d79708570a5f053ca9a3137",
                      "b9e6493edc014b09e6c1c7e216fb31630c9c222616e49b138d408a817dff9ba6",
                      "f04afaa8ae52acf275583dc5a1499a9f565ad35e5b9cb01e3765162d6b08a0d3",
                      "0e12c2eaae457732cc8d9954f19175a27c1b3553f541a59cb0a63a415c35aef2"),
}
INSTRUCTION = re.compile(
    r"^\s*([0-9A-F]{6})\s+((?:[0-9A-F]{2}\s+)+)\[\s*\d+\]\s+\d+\s+\S.*$", re.M)
LABEL = re.compile(r"^\s*([0-9A-F]{6})\s+\d+\s+(_\w+):\s*$", re.M)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def packet(stage):
    require(stage in ("arm", "run"), "Link fixture unknown admission stage")
    op, token = (0xa9, 0x36) if stage == "arm" else (0x56, 0xc9)
    return bytes((op, op ^ 255, CHANNEL, CHANNEL ^ 255, token, token ^ 255, 0x4c, 0xb3))


def decode(raw):
    require(type(raw) is bytes and len(raw) == SIZE and raw[:6] == b"M3LK\x01\x24" and
            raw[18:21] == bytes((CHANNEL, 5, 13)) and raw[29:] == b"\x69\x96" + bytes(5),
            "Link fixture signature/version/profile/guards changed")
    names = ("phase", "reason", "stage", "consumed", "attempts", "completed", "outcome",
             "clock_result", "tx_result", "owner_result", "frames", "before_tx")
    result = dict(zip(names, raw[6:18]))
    result.update(rx_polls=int.from_bytes(raw[21:23], "little"),
                  elapsed=int.from_bytes(raw[23:27], "little"),
                  remaining=int.from_bytes(raw[27:29], "little"))
    p, reason, stage = result["phase"], result["reason"], result["stage"]
    require(1 <= p <= 6 and p != 4 and reason <= 12 and (p == 6) == bool(reason) and
            stage <= 7 and result["consumed"] <= 1 and result["attempts"] <= 1 and
            result["completed"] <= 1 and result["outcome"] <= 4 and
            result["clock_result"] <= 9 and result["tx_result"] in (*range(19), 255) and
            result["owner_result"] in (*range(19), 255) and
            result["before_tx"] <= result["frames"] <= 2 and result["rx_polls"] <= 4096 and
            result["elapsed"] <= 0xffffff and result["remaining"] <= 256,
            "Link fixture live/invalid state or bounds")
    if p <= 3:
        require(not any(result[n] for n in ("reason", "stage", "consumed", "attempts", "completed",
                                            "outcome", "frames", "before_tx", "rx_polls", "elapsed")) and
                result["clock_result"] == 8 and result["tx_result"] == result["owner_result"] == 255 and
                (result["remaining"] > 0 if p < 3 else result["remaining"] == 0),
                "Link admission performed work or retained authorization")
    elif p == 5:
        require(stage == 7 and result["consumed"] == result["attempts"] == result["completed"] == 1 and
                not result["remaining"] and result["clock_result"] == 0 and result["owner_result"] == 5 and
                ((result["tx_result"] == 18 and result["outcome"] == 4) or
                 (result["tx_result"] == 17 and 1 <= result["outcome"] <= 3)),
                "Link END lacks real sequence completion")
        if result["outcome"] in (1, 2):
            require(result["frames"] > result["before_tx"], "Link RX outcome has no post-TX body")
    else:
        require(not result["completed"] and stage < 7, "Link FAULT claims completion")
        if reason in (1, 2):
            require(stage == result["consumed"] == result["attempts"] == 0 and
                    result["clock_result"] == 8 and result["tx_result"] == result["owner_result"] == 255 and
                    (reason != 1 or not result["remaining"]),
                    "Link admission fault performed work")
        elif reason != 3:
            stages = {4: (1,), 5: (2,), 7: (4,), 8: (3, 5, 6), 9: (3, 6),
                      10: (3, 5, 6), 11: (5,), 12: (5,)}
            require(stage in stages.get(reason, ()) and result["consumed"] == 1 and not result["remaining"] and
                    result["attempts"] == int(stage >= 4), "Link fault stage/history mismatch")
    return result


def decode_frames(record, raw):
    require(type(raw) is bytes and len(raw) == 256, "Link frame storage extent changed")
    frames = []
    for i in range(2):
        frame = raw[128*i:128*(i+1)]
        if i >= record["frames"]:
            require(frame == bytes(128), "Link unpublished frame storage changed")
            continue
        length, rssi, crc = frame[:3]
        require(3 <= length <= 125 and frame[3+length:] == bytes(125-length),
                "Link received length/inactive tail changed")
        frames.append(dict(before_tx=i < record["before_tx"], length=length,
                           rssi_raw=rssi, crc_correlation=crc, crc_ok=bool(crc & 128),
                           body_hex=frame[3:3+length].hex()))
    if record["phase"] == 5 and record["outcome"] in (1, 2):
        require(frames[record["before_tx"]]["crc_ok"] == (record["outcome"] == 1),
                "Link RX classification differs from actual CRC metadata")
    return frames


def check_end(record, clock, radio):
    require(len(clock) == 19 and len(radio) == 26, "Link diagnostic extent changed")
    if record["phase"] <= 3:
        require(clock == bytes(19) and radio == bytes(26), "Link admission touched diagnostics")
    if record["phase"] != 5:
        return
    require(clock[6] == 0 and clock[18] == 8 and clock[15:18] == b"\x88" * 3,
            "Link END lacks confirmed XOSC32")
    require(radio[8:10] == b"\x07\x05" and radio[12:14] == b"\x01\0" and
            not radio[14] & 0xc0 and not radio[15] & 0xe7 and radio[16] == 0 and
            radio[17] == radio[18] < 128 and radio[20] == 0 and radio[22] & 4 and radio[25] == 0,
            "Link END lacks physical stop and complete explicit drainage")


def verify_fixture(image, symbols, debug, board):
    require(board in HASHES, "Link fixture requires an exact board")
    size, code_sha, cdb_sha, map_sha, _ = HASHES[board]
    require(size <= 16384 and sha(code_bytes(image, size)) == code_sha,
            "Link complete CODE/constants/runtime changed")
    require(sha(debug.encode("ascii")) == cdb_sha, "Link complete raw CDB/ABI changed")
    require(sha(json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode("ascii")) == map_sha,
            "Link complete map changed")
    checkpoints = [symbols[n] for n in CHECKPOINTS]
    require(checkpoints == [8448 + BOARDS[board]*40, 8450 + BOARDS[board]*40, 8453 + BOARDS[board]*40] and
            bytes(image[a] for a in range(checkpoints[0], checkpoints[0]+8)) == b"\0\x22\0\x80\xfd\0\x80\xfd",
            "Link admission/terminal checkpoints changed")
    require(symbols["l_XSEG"] == 750 and symbols["s_SSEG"] == 0x21 and
            symbols["_radio_autoack_reserved_end"] == 334 and symbols["__gptrput_PARM_2"] == 749 and
            symbols["___memcpy_PARM_2"] == 738 and symbols["___memcpy_PARM_3"] == 741 and
            symbols["_memset_PARM_2"] == 746 and symbols["_memset_PARM_3"] == 747,
            "Link service/caller/complete runtime scratch boundary changed")
    for name, (address, length) in OBJECTS.items():
        key = "radio_link_fixture_" + name
        require(symbols["_" + key] == address and cdb_address(debug, f"L:G${key}$0_0$0") == address,
                "Link caller object moved")
        sizes = re.findall(rf"^S:G\${key}\$[^(]+\(\{{(\d+)\}}", debug, re.M)
        require(sizes and all(int(n) == length for n in sizes), "Link caller object ABI changed")
    body = cdb_address(debug, "L:Fradio_link_fixture_state$body$0_0$0")
    require(bytes(image[a] for a in range(body, body+13)) == BODY, "Link public synthetic body changed")
    require(not any(name.startswith(("_host_", "_radio_autoack_test", "_radio_tx_", "_radio_rx_",
                                    "_mac_tx_", "_mac_epoch_", "_mac_time_")) for name in symbols),
            "Link board image imported a model/foreign radio/timestamp owner")
    verify_clock_code(image, symbols, debug)
    verify_timebase_reader(image, symbols, debug, OBJECTS["state"][0], SIZE)
    return dict(checkpoints=checkpoints, reserved=334, helper=749,
                **{name: address for name, (address, _) in OBJECTS.items()})


def modules(board):
    return ("timebase", "clock", "radio_autoack", "startup", "status", board,
            "radio_link_fixture", "radio_link_fixture_state")


def records(text):
    return [(int(m[1], 16), bytes.fromhex(m[2])) for m in INSTRUCTION.finditer(text)]


def listing_identity(listings, board):
    parts = []
    for name in modules(board):
        text = listings[name]
        parts.append(name + "\n" + "".join(f"{a:06x}:{v.hex()}\n" for a, v in records(text)))
        parts.append("\n".join(line.strip() for line in text.splitlines()
                               if re.search(r"\b_\w+:|\.ds \d+$|\.area ", line)) + "\n")
    return sha("".join(parts).encode("ascii"))


def verify_listings(listings, image, symbols, board):
    require(set(listings) == set(modules(board)) and listing_identity(listings, board) == HASHES[board][4],
            "Link complete ordered listing/allocation/entry inventory changed")
    codes, covered, private, caller = {}, set(), set(), set()
    for name in modules(board):
        text = listings[name]; found = records(text); codes[name] = dict(found)
        require(f".module {name}" in text, "Link snapshot belongs to another module")
        for address, raw in found:
            span = set(range(address, address+len(raw)))
            require(not covered & span and all(image.get(address+i) == v for i, v in enumerate(raw)),
                    "Link duplicated/non-linked instruction")
            covered.update(span)
        if name in ("timebase", "clock", "radio_autoack", "radio_link_fixture_state"):
            segment = text.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
            target = caller if name == "radio_link_fixture_state" else private
            for address, length in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
                span = set(range(int(address, 16), int(address, 16)+int(length)))
                require(span and not span & (private | caller), "Link private/caller overlap")
                target.update(span)
    require(private == set(range(335)) and caller == set(range(335, 738)),
            "Link complete ownership prefix/scratch exclusion changed")
    for name in ("radio_link_fixture", "radio_link_fixture_state"):
        require(not peripheral_accesses(codes[name]) and
                not any(raw[0] == 0x90 and 0x6000 <= int.from_bytes(raw[1:], "big") < 0x6400
                        for raw in codes[name].values()), "Link caller bypasses genuine services")
    radio = codes["radio_autoack"]
    require([raw.hex() for _, raw, _ in peripheral_accesses(radio)] == [
        "e5a8", "e5b8", "e59a", "aebe", "e5d6", "e5d7", "e5c6", "b59e02",
        "e5bf", "e5e9", "e591", "85d982", "e5bf", "e5c6", "75913b",
        "75e1ee", "8bd9", "75913d", "75e1ea",
    ], "Link radio actual SFR inventory changed")
    sites = {}
    for name in ("timebase", "clock", "radio_autoack"):
        code = codes[name]
        for pc, raw, reg in peripheral_accesses(code):
            write = raw[0] == 0x75 or 0x88 <= raw[0] <= 0x8f
            observed = reg if write or raw[0] == 0xb5 else (
                raw[2] if raw[0] == 0x85 else raw[0]-0xa8 if 0xa8 <= raw[0] <= 0xaf else 0xe0)
            sites[pc] = ("w" if write else "r", reg, observed)
        for pc, raw in code.items():
            if raw[0] != 0x90 or not 0x6000 <= int.from_bytes(raw[1:], "big") < 0x6400:
                continue
            address = int.from_bytes(raw[1:], "big")
            if code.get(pc+3) == b"\xe0":
                sites[pc+3] = ("r", address, 0xe0)
            else:
                require(len(code.get(pc+3, b"")) == 2 and code[pc+3][0] == 0x74 and
                        code.get(pc+5) == b"\xf0", "Link unreviewed MMIO write addressing")
                sites[pc+5] = ("w", address, address)
    labels = {name: int(address, 16) for address, name in LABEL.findall(listings["radio_autoack"])}
    base, settle, rfd = (labels[name] for name in ("_setting_address", "_cca_settle", "_read_fifo"))
    require(radio.get(base+0x215) == b"\xe0" and radio.get(base+0x8e6) == b"\xf0" and
            bytes(image[a] for a in range(rfd, rfd+4)) == b"\x85\xd9\x82\x22" and
            sum(raw == b"\x12" + rfd.to_bytes(2, "big") for raw in radio.values()) == 1,
            "Link indexed configuration or exactly-once RFD leaf changed")
    sites[base+0x215] = ("r", None, 0xe0); sites[base+0x8e6] = ("w", None, None)
    require(bytes(image[a] for a in range(settle, settle+5)) == b"\0\0\0\0\x22" and
            sum(raw == b"\x12" + settle.to_bytes(2, "big") for raw in radio.values()) == 1,
            "Link genuine four-NOP CCA settling changed")
    sites[settle] = ("c", 0, None)
    return sites


def load_image(output, board):
    from debug_image import DebugImage
    output = Path(output)
    checked = DebugImage(output, board, "radio_link_fixture")
    stem = output / "radio_link_fixture"
    image = parse_ihex(stem.with_suffix(".ihx").read_text())
    symbols = parse_symbols(stem.with_suffix(".map").read_text())
    listings = {name: (output / f"radio_link_fixture.{name}.rst").read_text() for name in modules(board)}
    checked.radio_link_proof["sites"] = verify_listings(listings, image, symbols, board)
    return checked


def verify_memory(memory):
    require("Stack starts at: 0x21 (sp set to 0x20) with 223 bytes available" in memory and
            "ERROR" not in memory and "EXTERNAL RAM     0x0000   0x02ed     750" in memory,
            "Link linker accounting/error changed")
