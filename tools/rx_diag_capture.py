#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Offline IEEE 802.15.4 summary of a HWLab nrf802154 capture for the H2 RX investigation.

Input is the HWLab sniffer artifact as written (pcapng, one interface, link type 283
IEEE 802.15.4 TAP, TAP FCS type 0 = no FCS, ns timestamps on the HWLab session timeline
mapped from the sniffer's microsecond timer). Dissection uses tshark; the file structure
(link type, block integrity, interface statistics, exact MAC bytes) is checked here.

Never touches a device. Rules (docs/H2_RX_INVESTIGATION.md 10):
- Over-the-air presence is PROVEN only for frames in the file; absence is never proof
  (the sniffer can miss frames and reports no RF loss counter).
- FCS validity is UNKNOWN when the capture carries no FCS (HWLab: always).
- An ACK carries only a DSN: pairing is INFERRED, and AMBIGUOUS when several
  ACK-requesting frames or several ACKs share the DSN inside the window. HWLab does not
  document where in a frame its timestamp is taken (the real captures fit start of
  frame), so the window is the request's 250 kb/s airtime plus --ack-window-us.
- A retry is INFERRED only for byte-identical ACK-requesting frames within the window;
  equal DSN with different content is reported as DSN reuse, never as a retry.
- The CC2530 RX_DIAG block (rx_diag_decode.py report) is matched by MAC header content
  (FCF, DSN, PHR length) only. CC2530 Sleep Timer ticks and sniffer time are separate
  clock domains; no offset is assumed.
- 64-bit addresses are replaced by salted hashes (random salt per report unless --salt).
"""
import argparse
import hashlib
import json
from pathlib import Path
import secrets
import struct
import subprocess
import sys

LINK_TAP = 283
SHB, IDB, EPB, ISB = 0x0A0D0D0A, 1, 6, 5
FRAME_TYPES = {0: "beacon", 1: "data", 2: "ack", 3: "command"}
COMMANDS = {1: "association_request", 2: "association_response", 3: "disassociation_notification",
            4: "data_request", 5: "pan_id_conflict", 6: "orphan_notification", 7: "beacon_request",
            8: "coordinator_realignment", 9: "gts_request"}
TSHARK_FIELDS = ("frame.number", "frame.protocols", "wpan.frame_type", "wpan.cmd", "wpan.dst_pan",
                 "wpan.src_pan", "wpan.dst16", "wpan.src16", "wpan.dst64", "wpan.src64",
                 "zbee_nwk.frame_type", "zbee_nwk.security", "zbee_aps.type", "_ws.malformed")


class CaptureError(ValueError):
    pass


def options(body):
    out, i = {}, 0
    while i + 4 <= len(body):
        code, length = struct.unpack_from("<HH", body, i)
        if code == 0:
            break
        out.setdefault(code, []).append(body[i + 4:i + 4 + length])
        i += 4 + (length + 3 & ~3)
    return out


def read_pcapng(data):
    """Blocks of a little-endian pcapng file; a cut-short tail is reported, not hidden."""
    interfaces, packets, stats, i, incomplete = [], [], [], 0, None
    if len(data) < 12 or struct.unpack_from("<I", data)[0] != SHB:
        raise CaptureError("not a pcapng file")
    while i < len(data):
        if i + 12 > len(data):
            incomplete = f"{len(data) - i} trailing bytes"
            break
        kind, total = struct.unpack_from("<II", data, i)
        if total < 12 or total % 4 or i + total > len(data):
            incomplete = f"block at offset {i} cut short"
            break
        if struct.unpack_from("<I", data, i + total - 4)[0] != total:
            raise CaptureError(f"corrupt block length at offset {i}")
        body = data[i + 8:i + total - 4]
        if kind == SHB and struct.unpack_from("<I", body)[0] != 0x1A2B3C4D:
            raise CaptureError("big-endian or corrupt section header (HWLab writes little-endian)")
        if kind == IDB:
            link, = struct.unpack_from("<H", body)
            opt = options(body[8:])
            res = opt.get(9, [b"\x06"])[0][0]
            if res & 0x80:
                raise CaptureError("binary if_tsresol is not supported")
            interfaces.append({"link_type": link, "tsresol_exp10": res,
                               "name": opt.get(2, [b""])[0].decode(errors="replace"),
                               "description": opt.get(3, [b""])[0].decode(errors="replace")})
        elif kind == EPB:
            ifc, hi, lo, cap, orig = struct.unpack_from("<IIIII", body)
            if ifc >= len(interfaces):
                raise CaptureError("packet for an undeclared interface")
            ts = hi << 32 | lo
            scale = 10 ** (9 - interfaces[ifc]["tsresol_exp10"])
            packets.append({"ts_ns": ts * scale if scale >= 1 else ts // 10 ** (interfaces[ifc]["tsresol_exp10"] - 9),
                            "data": body[20:20 + cap], "truncated": cap != orig})
        elif kind == ISB:
            opt = options(body[12:])
            u64 = lambda c: struct.unpack("<Q", opt[c][0])[0] if c in opt else None  # noqa: E731
            stats.append({"comment": opt.get(1, [b""])[0].decode(errors="replace"),
                          "received": u64(4), "dropped": u64(5)})
        i += total
    return interfaces, packets, stats, incomplete


def tap(data):
    """IEEE 802.15.4 TAP header -> (metadata, MAC frame without FCS)."""
    if len(data) < 4 or data[0] != 0:
        raise CaptureError("unsupported TAP version")
    length, = struct.unpack_from("<H", data, 2)
    if length < 4 or length > len(data):
        raise CaptureError("TAP header longer than the packet")
    meta, i = {}, 4
    while i + 4 <= length:
        kind, size = struct.unpack_from("<HH", data, i)
        value = data[i + 4:i + 4 + size]
        if kind == 0 and size >= 1:
            meta["fcs_type"] = value[0]
        elif kind == 1 and size == 4:
            meta["rss_dbm"] = round(struct.unpack("<f", value)[0], 1)
        elif kind == 3 and size >= 3:
            meta["channel"], meta["page"] = struct.unpack_from("<HB", value)
        elif kind == 10 and size >= 1:
            meta["lqi"] = value[0]
        i += 4 + (size + 3 & ~3)
    mac = data[length:]
    fcs = {0: 0, 1: 2, 2: 4}.get(meta.get("fcs_type"))
    if fcs is None:
        raise CaptureError(f"unknown TAP FCS type {meta.get('fcs_type')}")
    return meta, (mac[:-fcs] if fcs else mac), fcs


def tshark_rows(path, tshark):
    cmd = [tshark, "-n", "-r", str(path), "-T", "fields", "-E", "separator=\t", "-E", "occurrence=f"]
    for f in TSHARK_FIELDS:
        cmd += ["-e", f]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    cut = "cut short" in out.stderr or "truncated" in out.stderr
    if out.returncode and not cut:
        raise CaptureError(f"tshark failed: {out.stderr.strip()}")
    version = subprocess.run([tshark, "-v"], capture_output=True, text=True).stdout.splitlines()[0]
    rows = [dict(zip(TSHARK_FIELDS, line.split("\t"))) for line in out.stdout.splitlines() if line]
    return rows, version, cut


def number(text):
    return int(text, 0) if text else None


def frame_record(index, packet, row, salt, origin):
    meta, mac, fcs = tap(packet["data"])
    if len(mac) < 3:
        raise CaptureError(f"frame {index}: MAC frame shorter than FCF+DSN")
    fcf, dsn = mac[0] | mac[1] << 8, mac[2]
    kind = FRAME_TYPES.get(fcf & 7, f"reserved{fcf & 7}")
    protocols = row.get("frame.protocols", "")
    cmd = number(row.get("wpan.cmd"))
    label = COMMANDS.get(cmd, f"command_{cmd}") if kind == "command" else kind
    if kind == "data":
        label = "aps" if "zbee_aps" in protocols else "nwk" if "zbee_nwk" in protocols else "data"
    hashed = lambda v: hashlib.sha256(salt + v.lower().encode()).hexdigest()[:12] if v else None  # noqa: E731
    return {
        "index": index, "ts_ns": packet["ts_ns"], "t_ms": round((packet["ts_ns"] - origin) / 1e6, 3),
        "channel": meta.get("channel"), "kind": label, "frame_type": kind, "fcf": f"{fcf:#06x}", "dsn": dsn,
        "mac_length": len(mac), "phr_length": len(mac) + 2,
        "ack_request": bool(fcf & 0x20), "security": bool(fcf & 8), "pending": bool(fcf & 0x10),
        "dst_pan": row.get("wpan.dst_pan") or None, "src_pan": row.get("wpan.src_pan") or None,
        "dst16": row.get("wpan.dst16") or None, "src16": row.get("wpan.src16") or None,
        "dst64_hash": hashed(row.get("wpan.dst64")), "src64_hash": hashed(row.get("wpan.src64")),
        "nwk_security": row.get("zbee_nwk.security") in ("1", "True") if "zbee_nwk" in protocols else None,
        "rss_dbm": meta.get("rss_dbm"), "lqi": meta.get("lqi"),
        "fcs": "UNKNOWN (no FCS in capture)" if fcs == 0 else "STRIPPED (not validated here)",
        "malformed": bool(row.get("_ws.malformed")), "truncated": packet["truncated"],
        "content_id": hashlib.sha256(salt + mac).hexdigest()[:16], "_mac": mac,
    }


def airtime_ns(frame):
    """2.4 GHz O-QPSK: 32 us/octet for preamble+SFD+PHR (6) and the PSDU (incl. FCS)."""
    return (frame["phr_length"] + 6) * 32000


def pair_acks(frames, window_ns):
    requests = [f for f in frames if f["ack_request"] and f["frame_type"] != "ack"]
    acks = [f for f in frames if f["frame_type"] == "ack"]
    for f in requests:
        f["ack"] = {"status": "NO_ACK_OBSERVED", "class": "UNKNOWN", "candidates": []}
    for f in frames:
        if not f["ack_request"] and f["frame_type"] != "ack":
            f["ack"] = {"status": "NOT_REQUESTED"}
    for a in acks:
        owners = [r for r in requests if r["dsn"] == a["dsn"]
                  and 0 < a["ts_ns"] - r["ts_ns"] <= airtime_ns(r) + window_ns]
        a["ack"] = {"status": "UNPAIRED_ACK" if not owners else "PAIRED" if len(owners) == 1 else "AMBIGUOUS",
                    "class": "UNKNOWN" if not owners else "INFERRED",
                    "candidates": [r["index"] for r in owners]}
        for r in owners:
            r["ack"]["candidates"].append(a["index"])
    for r in requests:
        n = len(r["ack"]["candidates"])
        if n:
            shared = any(len(next(a for a in acks if a["index"] == c)["ack"]["candidates"]) > 1
                         for c in r["ack"]["candidates"])
            r["ack"].update(status="PAIRED" if n == 1 and not shared else "AMBIGUOUS", class_="INFERRED")
            r["ack"]["class"] = r["ack"].pop("class_")
    return requests, acks


def retries(frames, window_ns):
    groups, reuse, last = [], [], {}
    for f in frames:
        if f["frame_type"] == "ack":
            continue
        key = (f["frame_type"], f["src16"], f["src64_hash"], f["dst16"], f["dst64_hash"], f["dst_pan"], f["dsn"])
        prev = last.get(key)
        if prev and f["ts_ns"] - prev[-1]["ts_ns"] <= window_ns:
            if f["_mac"] == prev[0]["_mac"] and f["ack_request"]:
                prev.append(f)
                continue
            reuse.append({"first": prev[0]["index"], "second": f["index"], "dsn": f["dsn"],
                          "class": "PROVEN", "note": "same DSN/addresses, different content: not a retry"})
        last[key] = [f]
        groups.append(last[key])
    return [{"frames": [x["index"] for x in g], "dsn": g[0]["dsn"], "kind": g[0]["kind"], "class": "INFERRED",
             "note": "byte-identical ACK-requesting frames; sniffer may have missed further attempts"}
            for g in groups if len(g) > 1], reuse


def match_headers(frames, diag, after_ms):
    """Content-only matches for the CC2530 headers; no clock alignment."""
    out = {}
    first = diag.get("first_overflow") or {}
    for name, header in (("last_good_mac_header", diag.get("last_good_mac_header")),
                         ("first_overflow.last_good_before", first.get("last_good_before"))):
        if not header:
            out[name] = {"status": "NOT_RECORDED_BY_CC2530"}
            continue
        hits = [f for f in frames if f["fcf"] == header["fcf"] and f["dsn"] == header["dsn"]
                and f["phr_length"] == header["phr_length"]]
        contents = sorted({f["content_id"] for f in hits})
        entry = {"header": {k: header[k] for k in ("fcf", "dsn", "phr_length")},
                 "candidates": [f["index"] for f in hits], "distinct_contents": len(contents),
                 "status": "NOT_IN_CAPTURE" if not hits else "UNIQUE_CONTENT" if len(contents) == 1 else "AMBIGUOUS",
                 "class": "UNKNOWN" if len(contents) != 1 else "INFERRED",
                 "note": "header match only (DSN wraps at 256; header bytes do not identify the payload)"}
        if name.startswith("first_overflow") and len(contents) == 1:
            start = hits[-1]["ts_ns"]
            entry["air_after_last_candidate"] = [
                {k: f[k] for k in ("index", "t_ms", "kind", "dsn", "dst16", "phr_length")}
                for f in frames if start < f["ts_ns"] <= start + after_ms * 1e6]
            entry["air_after_note"] = ("frames on air after the LAST matching candidate within the window; "
                                       "the overflow time is not known in sniffer time")
        out[name] = entry
    return out


def analyse(path, tshark="tshark", salt=None, ack_window_us=1000, retry_window_ms=100, rx_diag=None,
            device_short=None, after_ms=50):
    data = Path(path).read_bytes()
    interfaces, packets, stats, incomplete = read_pcapng(data)
    links = sorted({i["link_type"] for i in interfaces})
    if links != [LINK_TAP]:
        raise CaptureError(f"expected one link type {LINK_TAP} (IEEE 802.15.4 TAP), found {links}")
    rows, version, cut = tshark_rows(path, tshark)
    if len(rows) != len(packets) or any(int(r["frame.number"]) != i + 1 for i, r in enumerate(rows)):
        raise CaptureError(f"tshark dissected {len(rows)} frames, file has {len(packets)} complete packets")
    salt_bytes = salt.encode() if salt else secrets.token_bytes(16)
    origin = packets[0]["ts_ns"] if packets else 0
    frames = [frame_record(i + 1, p, r, salt_bytes, origin) for i, (p, r) in enumerate(zip(packets, rows))]
    requests, acks = pair_acks(frames, ack_window_us * 1000)
    retry, reuse = retries(frames, retry_window_ms * 1e6)
    kinds = {}
    for f in frames:
        kinds[f["kind"]] = kinds.get(f["kind"], 0) + 1
    channels = sorted({f["channel"] for f in frames if f["channel"] is not None})
    meta_missing = {k: sum(f[k] is None for f in frames) for k in ("rss_dbm", "lqi", "channel")}
    report = {
        "schema": "cc2530-rx-diag-capture/1",
        "capture": {"path": str(path), "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
                    "link_types": links, "interfaces": interfaces, "packets": len(packets),
                    "incomplete": incomplete or ("tshark: cut short" if cut else None),
                    "statistics": stats[-1] if stats else None,
                    "statistics_note": "HWLab ISB: received = frames reported by the sniffer; dropped = live "
                                       "event-stream drops (the file still holds every reported frame); "
                                       "no over-the-air loss counter exists" if stats else
                                       "no interface statistics block: capture was not closed cleanly",
                    "tshark": version},
        "clock": {"domain": "HWLab session ns (sniffer us timer mapped by HWLab); not CC2530 time",
                  "origin_ts_ns": origin, "cc2530_alignment": "none (content matches only)"},
        "limitations": {"fcs": "UNKNOWN: capture carries no FCS (TAP FCS type 0)" if all(
                            f["fcs"].startswith("UNKNOWN") for f in frames) else "mixed/stripped FCS",
                        "missing_metadata": meta_missing,
                        "absence": "a frame absent from the capture is UNKNOWN, not proven absent"},
        "channels": channels,
        "counts": dict(sorted(kinds.items())),
        "ack": {s: sum(r["ack"]["status"] == s for r in requests) for s in ("PAIRED", "AMBIGUOUS", "NO_ACK_OBSERVED")}
               | {"unpaired_acks": sum(a["ack"]["status"] == "UNPAIRED_ACK" for a in acks),
                  "window": f"request airtime + {ack_window_us} us"},
        "retry_candidates": retry, "dsn_reuse": reuse, "retry_window_ms": retry_window_ms,
    }
    if device_short is not None:
        mine = f"{device_short:#06x}"
        report["device"] = {"short": mine,
                            "addressed": sum(f["dst16"] == mine for f in frames),
                            "sent": sum(f["src16"] == mine for f in frames)}
    if rx_diag is not None:
        report["cc2530_match"] = match_headers(frames, rx_diag, after_ms)
    report["frames"] = [{k: v for k, v in f.items() if k != "_mac"} for f in frames]
    return report


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("capture", type=Path, help="HWLab nrf802154-chNN.pcapng artifact")
    p.add_argument("--tshark", default="tshark")
    p.add_argument("--rx-diag", type=Path, help="rx_diag_decode.py JSON report for the same trial")
    p.add_argument("--device-short", type=lambda v: int(v, 0), help="CC2530 short address, if known")
    p.add_argument("--salt", help="fixed salt for 64-bit address/content hashes (default: random)")
    p.add_argument("--ack-window-us", type=int, default=1000,
                   help="allowed ACK delay after the request airtime")
    p.add_argument("--retry-window-ms", type=int, default=100)
    p.add_argument("--after-ms", type=int, default=50)
    p.add_argument("--output", type=Path, help="full JSON (frames included); stdout gets the summary")
    args = p.parse_args(argv)
    try:
        diag = json.loads(args.rx_diag.read_text()) if args.rx_diag else None
        report = analyse(args.capture, args.tshark, args.salt, args.ack_window_us, args.retry_window_ms,
                         diag, args.device_short, args.after_ms)
    except (CaptureError, OSError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"rx_diag_capture: {error}", file=sys.stderr)
        return 2
    if args.output:
        args.output.write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "frames"}, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
