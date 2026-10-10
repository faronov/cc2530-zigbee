#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Decode a CC2530_RX_DIAG v1 block (include/cc2530_rx_diag.h) read from a halted CPU.

Offline only; never touches a device. The block exists only in the LLVM
rx-diag image (SDCC cannot fit it; docs/H2_RX_INVESTIGATION.md 10). Its address
is taken from the linked ELF actually flashed, never assumed, and must equal the
v1 ABI address. Values are what the firmware
recorded: "PROVEN" below means "recorded by the firmware in this block", not
an over-the-air fact; absence of a record is reported as NOT_RECORDED, and
unknown/saturated values explicitly.

Read the block with the existing HWLab XDATA API on an already halted CPU, e.g.
  rx_diag_decode.py --elf hw1.elf --print-read-command
  hwlab target memory read ... --space xdata --address 0x1E40 --length 80 --path rx_diag.bin
  rx_diag_decode.py --elf hw1.elf --dump rx_diag.bin --output rx_diag.json
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
ADDRESS, SIZE, VERSION, LIMIT = 0x1E40, 80, 1, 0x1F00
SIGNATURE = b"RXDG"
TICK_HZ = 32768  # nominal Sleep Timer rate; RCOSC/XOSC tolerance is not known here
SITES = {1: "observe (FSMSTAT1/RFERRF sample)", 2: "consume (FIFO read path)"}
FLAGS = {1: "SERVICE_VALID", 2: "OFF_VALID"}

# (name, struct format, offset) -- must match RX_DIAG_AT() in include/cc2530_rx_diag.h.
FIELDS = (
    ("signature", "4s", 0), ("version", "B", 4), ("size", "B", 5),
    ("overflows", "B", 6), ("salvaged", "B", 7), ("flushes", "B", 8), ("rx_lost", "B", 9),
    ("frames", "H", 10), ("bad_crc", "B", 12), ("first_valid", "B", 13),
    ("max_service_gap", "H", 14), ("max_rx_off_gap", "H", 16),
    ("first_site", "B", 18), ("first_radio_state", "B", 19), ("first_radio_phase", "B", 20),
    ("first_rferrf", "B", 21), ("first_fsmstat0", "B", 22), ("first_fsmstat1", "B", 23),
    ("first_rxfifocnt", "B", 24), ("first_rxfirst", "B", 25), ("first_rxlast", "B", 26),
    ("flags", "B", 27), ("first_stamp", "I", 28), ("first_gap", "H", 32), ("first_frames", "H", 34),
    ("last_header", "4s", 36), ("first_header", "4s", 40),
    ("service_stamp", "I", 44), ("off_stamp", "I", 48),
    ("ctx_valid", "B", 52), ("ctx_bdb_phase", "B", 53), ("ctx_workspace", "B", 54),
    ("ctx_adapter_phase", "B", 55), ("ctx_adapter_goal", "B", 56),
    ("fail_valid", "B", 57), ("fail_bdb_phase", "B", 58), ("fail_result", "B", 59),
    ("fail_workspace", "B", 60), ("fail_aps_result", "B", 61), ("fail_aps_retries", "B", 62),
    ("fail_aps_error", "B", 63),
    ("tx_valid", "B", 64), ("tx_outcome", "B", 65), ("tx_retries", "B", 66), ("tx_nb", "B", 67),
    ("tx_special", "B", 68), ("pad", "B", 69), ("last_service_gap", "H", 70), ("reserved", "8s", 72),
)
# Symbolic names come from the firmware source matching the image (--source).
NAMES = {
    "first_radio_state": ("enum", "RADIO_AUTOACK_COLD"),
    "ctx_bdb_phase": ("enum", "BDB_JOIN_READY"), "fail_bdb_phase": ("enum", "BDB_JOIN_READY"),
    "fail_result": ("enum", "BDB_JOIN_TRANSMIT_FAILED"),
    "ctx_workspace": ("define", "BDB_JOIN_WORK_NONE"), "fail_workspace": ("define", "BDB_JOIN_WORK_NONE"),
    "ctx_adapter_phase": ("enum", "MAC_ADAPTER_COLLECT"), "ctx_adapter_goal": ("enum", "CLOSE_RETIRE"),
    "tx_outcome": ("define", "MAC_TX_OUTCOME_NONE"),
}


class DecodeError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise DecodeError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def layout():
    return [{"field": n, "offset": o, "size": struct.calcsize("<" + f)} for n, f, o in FIELDS]


def header_layout(path=ROOT / "include/cc2530_rx_diag.h"):
    """Offsets/constants declared by the C header (cross-checked by the tests)."""
    text = path.read_text()
    offsets = {m[1]: int(m[2]) for m in re.finditer(r"^RX_DIAG_AT\((\w+), (\d+)\);", text, re.M)}
    const = {m[1]: int(m[2], 0) for m in re.finditer(
        r"^#define CC2530_RX_DIAG_(ADDRESS|SIZE|VERSION|LIMIT) (0x[0-9a-fA-F]+|\d+)", text, re.M)}
    return offsets, const


def address_from_elf(path):
    from elftools.elf.elffile import ELFFile
    with open(path, "rb") as handle:
        elf = ELFFile(handle)
        found = [s for s in elf.get_section_by_name(".symtab").iter_symbols() if s.name == "cc2530_rx_diag"]
        require(len(found) == 1, f"{path}: no unique cc2530_rx_diag symbol (not an RX_DIAG image?)")
        section = elf.get_section(found[0]["st_shndx"])
        require(section.name == ".mcs51_rx_diag" and section["sh_type"] == "SHT_NOBITS",
                f"{path}: cc2530_rx_diag is not in the .mcs51_rx_diag NOBITS section")
        return found[0]["st_value"], found[0]["st_size"], "LLVM ELF symbol cc2530_rx_diag"


def locate(elf):
    image = Path(elf)
    address, size, how = address_from_elf(image)
    require((address, size) == (ADDRESS, SIZE),
            f"image places the block at {address:#x}/{size}, ABI v1 is {ADDRESS:#x}/{SIZE}")
    require(address + size <= LIMIT, "block reaches the IRAM alias")
    return {"image": str(image), "image_sha256": sha(image.read_bytes()), "address": address,
            "size": size, "address_source": how}


def source_names(root):
    """Value->name tables from enums/#define blocks in the firmware source."""
    texts = [re.sub(r"/\*.*?\*/|//[^\n]*", "", p.read_text(errors="replace"), flags=re.S)
             for d in ("include", "src") for p in sorted((root / d).glob("*.[ch]"))]
    enums, defines = {}, {}
    for text in texts:
        for body in re.findall(r"\benum\b[^{;]*\{([^}]*)\}", text):
            value, table = -1, {}
            for item in (i.strip() for i in body.split(",")):
                if not item:
                    continue
                name, _, given = (x.strip() for x in item.partition("="))
                value = int(given.rstrip("uUlL"), 0) if given else value + 1
                table[name] = value
            for name in table:
                enums.setdefault(name, table)
        lines = text.splitlines()
        for index, line in enumerate(lines):
            m = re.match(r"#define (\w+) (\d+)[uU]?\s*$", line)
            if not m or m[1] in defines:
                continue
            table = {}
            for follow in lines[index:]:
                n = re.match(r"#define (\w+) (\d+)[uU]?\s*$", follow)
                if not n:
                    break
                table[n[1]] = int(n[2])
            defines[m[1]] = table
    tables = {}
    for field, (kind, anchor) in NAMES.items():
        table = (enums if kind == "enum" else defines).get(anchor)
        if table:
            tables[field] = {v: k for k, v in table.items()}
    return tables


def ticks(value):
    if value == 0xFFFF:
        return {"ticks": None, "status": "UNKNOWN", "note": "no valid previous stamp"}
    entry = {"ticks": value, "ms_nominal": round(value * 1000 / TICK_HZ, 3), "status": "PROVEN"}
    if value == 0xFFFE:
        entry.update(status="PROVEN_LOWER_BOUND", note="saturated: >= 0xfffe ticks")
    return entry


def mac_header(raw):
    fcf = raw[0] | raw[1] << 8
    kinds = {0: "beacon", 1: "data", 2: "ack", 3: "command"}
    return {"fcf": f"{fcf:#06x}", "frame_type": kinds.get(fcf & 7, f"reserved({fcf & 7})"),
            "security": bool(fcf & 8), "pending": bool(fcf & 16), "ack_request": bool(fcf & 32),
            "pan_id_compression": bool(fcf & 64), "dst_mode": fcf >> 10 & 3,
            "frame_version": fcf >> 12 & 3, "src_mode": fcf >> 14 & 3,
            "dsn": raw[2], "phr_length": raw[3]}


def decode(block, names=None):
    require(len(block) == SIZE, f"dump is {len(block)} bytes, v1 block is {SIZE}")
    raw = {n: struct.unpack_from("<" + f, block, o)[0] for n, f, o in FIELDS}
    require(raw["signature"] == SIGNATURE, f"bad signature {raw['signature']!r} (wrong address/image or "
            "not reset by an RX_DIAG image)")
    require(raw["version"] == VERSION and raw["size"] == SIZE,
            f"unsupported RX_DIAG version {raw['version']}/size {raw['size']}")
    require(raw["pad"] == 0 and raw["reserved"] == bytes(8),
            "pad/reserved bytes are non-zero (corrupted or foreign block)")
    require(not raw["flags"] & ~3, f"undefined flag bits {raw['flags']:#04x}")
    names = names or {}

    def named(field):
        value = raw[field]
        return {"value": value, "name": names.get(field, {}).get(value)}

    consistency = []
    def check(ok, text):
        consistency.append({"check": text, "ok": bool(ok)})
    check(bool(raw["first_valid"]) == (raw["overflows"] > 0), "first_valid iff overflows > 0")
    check(raw["first_frames"] <= raw["frames"], "first_frames <= frames")
    check(raw["flushes"] <= raw["overflows"] or raw["overflows"] == 255, "flushes <= overflows (one per episode)")
    check(raw["rx_lost"] <= raw["flushes"] or raw["flushes"] == 255, "rx_lost <= flushes")
    check(raw["first_valid"] or not any(raw[k] for k in ("first_site", "first_stamp", "first_rferrf")),
          "no first-overflow snapshot without first_valid")
    for group in ("ctx", "fail", "tx"):
        fields = [n for n, _, _ in FIELDS if n.startswith(group + "_") and n != group + "_valid"]
        check(raw[group + "_valid"] or not any(raw[f] for f in fields), f"no {group} snapshot without {group}_valid")

    def counter(field, width=255):
        value = raw[field]
        return {"value": value, "status": "PROVEN_LOWER_BOUND" if value == width else "PROVEN"}

    result = {
        "header": {"signature": "RXDG", "version": raw["version"], "size": raw["size"]},
        "counters": {k: counter(k) for k in ("overflows", "salvaged", "flushes", "rx_lost", "bad_crc")},
        "flags": [n for bit, n in FLAGS.items() if raw["flags"] & bit],
        "max_service_gap": ticks(raw["max_service_gap"]),
        "max_rx_off_gap": ticks(raw["max_rx_off_gap"]),
        "last_service_gap": ticks(raw["last_service_gap"]),
        "service_stamp": raw["service_stamp"], "off_stamp": raw["off_stamp"],
        "last_good_mac_header": mac_header(raw["last_header"]) if raw["frames"] else None,
    }
    result["counters"]["frames"] = counter("frames", 0xFFFF)
    if raw["max_rx_off_gap"] == 0:
        result["max_rx_off_gap"] = {"ticks": 0, "status": "PROVEN",
                                    "note": "no stop-to-resume interval recorded"}
    if raw["first_valid"]:
        result["first_overflow"] = {
            "status": "PROVEN", "site": SITES.get(raw["first_site"], raw["first_site"]),
            "radio_state": named("first_radio_state"), "radio_phase": raw["first_radio_phase"],
            "rferrf": f"{raw['first_rferrf']:#04x}", "rxoverf": bool(raw["first_rferrf"] & 4),
            "fsmstat0": f"{raw['first_fsmstat0']:#04x}", "fsmstat1": f"{raw['first_fsmstat1']:#04x}",
            "fifo_fifop": f"{raw['first_fsmstat1'] >> 6 & 3:02b}",
            "rxfifocnt": raw["first_rxfifocnt"], "rxfirst": raw["first_rxfirst"],
            "rxlast": raw["first_rxlast"], "stamp_ticks24": raw["first_stamp"],
            "service_gap": ticks(raw["first_gap"]), "frames_before": raw["first_frames"],
            "last_good_before": mac_header(raw["first_header"]) if raw["first_frames"] else None,
            "note": "consume-site FIFO count/first/last come from the preceding observe sample"
                    if raw["first_site"] == 2 else None,
        }
    else:
        result["first_overflow"] = {"status": "NOT_RECORDED"}
    result["first_loss_context"] = {
        "status": "PROVEN", "bdb_phase": named("ctx_bdb_phase"), "workspace": named("ctx_workspace"),
        "adapter_phase": named("ctx_adapter_phase"), "adapter_goal": named("ctx_adapter_goal"),
    } if raw["ctx_valid"] else {"status": "NOT_RECORDED"}
    result["first_bdb_failure"] = {
        "status": "PROVEN", "bdb_phase": named("fail_bdb_phase"), "result": named("fail_result"),
        "workspace": named("fail_workspace"),
        "aps": {"result": raw["fail_aps_result"], "retries": raw["fail_aps_retries"],
                "error": raw["fail_aps_error"]} if raw["fail_workspace"] == 3 else None,
    } if raw["fail_valid"] else {"status": "NOT_RECORDED"}
    result["first_uplink_failure"] = {
        "status": "PROVEN", "outcome": named("tx_outcome"), "retries": raw["tx_retries"],
        "nb": raw["tx_nb"], "kind": "APS ACK" if raw["tx_special"] == 0x80 else
        {"special": raw["tx_special"]},
        "note": "local MAC result only; over-the-air delivery needs the sniffer/coordinator",
    } if raw["tx_valid"] else {"status": "NOT_RECORDED"}
    result["consistency"] = consistency
    result["raw_hex"] = block.hex()
    return result


def read_command(address):
    return ("hwlab target memory read --plugin ccdebugger --yes --config backend=native "
            "--config access=existing-debug-session --config allow_memory_access=true "
            f"--space xdata --address 0x{address:04X} --length {SIZE} --path rx_diag.bin")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--elf", help="linked LLVM rx-diag ELF of the flashed image")
    parser.add_argument("--dump", type=Path, help="raw 80-byte XDATA dump")
    parser.add_argument("--source", type=Path, default=ROOT, help="firmware tree matching the image (names)")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--print-read-command", action="store_true")
    parser.add_argument("--layout", action="store_true", help="print the v1 layout and exit")
    args = parser.parse_args(argv)
    if args.layout:
        print(json.dumps({"address": ADDRESS, "size": SIZE, "version": VERSION, "fields": layout()}, indent=1))
        return 0
    try:
        require(args.elf is not None, "--elf required (the block address comes from the linked image)")
        where = locate(args.elf)
        if args.print_read_command:
            print(read_command(where["address"]))
            return 0
        require(args.dump is not None, "--dump required")
        block = args.dump.read_bytes()
        report = {"schema": "cc2530-rx-diag-decode/1", "image": where,
                  "dump": {"path": str(args.dump), "sha256": sha(block)},
                  "clock": {"unit": "24-bit Sleep Timer ticks at radio-operation start",
                            "nominal_hz": TICK_HZ, "domain": "CC2530 only; not sniffer/host time"},
                  **decode(block, source_names(args.source))}
    except (DecodeError, OSError) as error:
        print(f"rx_diag_decode: {error}", file=sys.stderr)
        return 2
    text = json.dumps(report, indent=1)
    if args.output:
        args.output.write_text(text + "\n")
    print(text)
    return 0 if all(c["ok"] for c in report["consistency"]) else 1


if __name__ == "__main__":
    sys.exit(main())
