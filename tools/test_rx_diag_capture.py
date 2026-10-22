#!/usr/bin/env python3
"""Offline tests for tools/rx_diag_capture.py on small synthetic HWLab-format captures.

The pcapng builder mirrors HWLab internal/pcap/pcapng.go (SHB, one IDB with link type
283 and ns tsresol, EPBs carrying an IEEE 802.15.4 TAP header with FCS type 0/RSS/
channel/LQI TLVs, closing ISB with comment/received/dropped). No RF, no device.
Dissection tests need tshark (TSHARK=... or on PATH) and are skipped without it.
"""
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rx_diag_capture as rc  # noqa: E402

TSHARK = os.environ.get("TSHARK") or shutil.which("tshark")
PAN, DEV64, COORD64 = b"\x62\x1a", bytes.fromhex("0403020100") + b"\x4b\x12\x00", bytes.fromhex("ddccbbaa004b1200")


def opt(code, value):
    return struct.pack("<HH", code, len(value)) + value + bytes(-len(value) % 4)


def block(kind, body):
    body += bytes(-len(body) % 4)
    return struct.pack("<II", kind, len(body) + 12) + body + struct.pack("<I", len(body) + 12)


def tap_header(fcs=0, rss=-60.0, channel=15, lqi=200, extra=b""):
    tlvs = opt(0, bytes([fcs]))
    if rss is not None:
        tlvs += opt(1, struct.pack("<f", rss))
    if channel is not None:
        tlvs += opt(3, struct.pack("<HB", channel, 0))
    if lqi is not None:
        tlvs += opt(10, bytes([lqi]))
    return struct.pack("<BBH", 0, 0, 4 + len(tlvs)) + tlvs + extra


def pcapng(packets, link=283, close=True):
    out = block(0x0A0D0D0A, struct.pack("<IHHq", 0x1A2B3C4D, 1, 0, -1) + opt(4, b"hwlab nrf802154") + opt(0, b""))
    out += block(1, struct.pack("<HHI", link, 0, 262144) + opt(2, b"ch15") +
                 opt(3, b"synthetic sniffer, timestamps are hwlab session ns") + opt(9, b"\x09") + opt(0, b""))
    for ts_us, data in packets:
        ns = 5_000_000_000 + ts_us * 1000
        out += block(6, struct.pack("<IIIII", 0, ns >> 32, ns & 0xFFFFFFFF, len(data), len(data)) + data)
    if close:
        out += block(5, struct.pack("<III", 0, 0, 0) + opt(1, b"clock.source=synthetic\n") +
                     opt(4, struct.pack("<Q", len(packets))) + opt(5, struct.pack("<Q", 0)) + opt(0, b""))
    return out


def mac(fcf, dsn, rest=b""):
    return struct.pack("<HB", fcf, dsn) + rest


def short(v):
    return struct.pack("<H", v)


def scenario():
    """(t_us, MAC frame, TAP kwargs) of a small association-like exchange."""
    assoc_resp = mac(0xCC63, 0x80, PAN + DEV64 + COORD64 + b"\x02" + short(0x1234) + b"\x00")
    return [
        (0, mac(0xC823, 0x10, PAN + short(0) + b"\xff\xff" + DEV64 + b"\x01\x80"), {}),   # 1 assoc request
        (1164, mac(0x0002, 0x10), {}),                                                  # 2 ACK
        (10000, mac(0xC863, 0x11, PAN + short(0) + DEV64 + b"\x04"), {}),               # 3 data request
        (11018, mac(0x0012, 0x11), {}),                                                 # 4 ACK, pending
        (20000, assoc_resp, {}),                                                        # 5 assoc response
        (23000, assoc_resp, {}),                                                        # 6 retry, same bytes
        (24256, mac(0x0002, 0x80), {}),                                                 # 7 ACK
        (40000, mac(0x8861, 0x81, PAN + short(0) + short(0x1234) + b"\xde\xad"), {}),   # 8 data
        (40200, mac(0x8861, 0x81, PAN + short(0) + short(0x5678) + b"\xbe\xef"), {}),   # 9 data, same DSN
        (41000, mac(0x0002, 0x81), {}),                                                 # 10 ACK: ambiguous
        (60000, mac(0x8861, 0x82, PAN + short(0) + short(0x1234) + b"\x01\x02"), {}),   # 11 no ACK seen
        (70000, mac(0x8841, 0x83, PAN + short(0xFFFF) + short(0x1234) + b"\x03"), {}),  # 12 broadcast
        (80000, mac(0x0002, 0x99), {}),                                                 # 13 orphan ACK
        (90000, mac(0x8861, 0x84, PAN + short(0) + short(0x1234) + b"\x0a"), {}),       # 14
        (95000, mac(0x8861, 0x84, PAN + short(0) + short(0x1234) + b"\x0b"), {}),       # 15 DSN reuse
        (100000, mac(0x8000, 0x05, PAN + short(0) + b"\xff\xcf\x00\x00"),               # 16 beacon, no RSS/LQI
         {"rss": None, "lqi": None}),
    ]


def capture_bytes(frames=None, **kw):
    return pcapng([(t, tap_header(**meta) + frame) for t, frame, meta in (frames or scenario())], **kw)


class Structure(unittest.TestCase):
    def test_reads_hwlab_layout(self):
        interfaces, packets, stats, incomplete = rc.read_pcapng(capture_bytes())
        self.assertEqual([(i["link_type"], i["tsresol_exp10"], i["name"]) for i in interfaces], [(283, 9, "ch15")])
        self.assertEqual(len(packets), 16)
        self.assertEqual(packets[1]["ts_ns"] - packets[0]["ts_ns"], 1_164_000)
        self.assertEqual(stats, [{"comment": "clock.source=synthetic\n", "received": 16, "dropped": 0}])
        self.assertIsNone(incomplete)

    def test_tap_metadata_and_fcs(self):
        meta, frame, fcs = rc.tap(tap_header() + b"\x02\x00\x10")
        self.assertEqual((meta, frame, fcs), ({"fcs_type": 0, "rss_dbm": -60.0, "channel": 15, "page": 0,
                                              "lqi": 200}, b"\x02\x00\x10", 0))
        self.assertEqual(rc.tap(tap_header(fcs=1) + b"\x02\x00\x10\xaa\xbb")[1], b"\x02\x00\x10")
        self.assertEqual(rc.tap(tap_header(rss=None, lqi=None, channel=None) + b"\x02\x00\x10")[0], {"fcs_type": 0})
        for bad in (b"\x01\x00\x08\x00", tap_header(fcs=7) + b"\x02\x00\x10", b"\x00\x00\x40\x00"):
            with self.assertRaises(rc.CaptureError):
                rc.tap(bad)

    def test_incomplete_and_unclosed(self):
        data = capture_bytes()
        _, packets, _, incomplete = rc.read_pcapng(data[:-30])
        self.assertIn("cut short", incomplete)
        self.assertEqual(len(packets), 16)  # ISB lost, every EPB intact
        _, packets, stats, incomplete = rc.read_pcapng(capture_bytes(close=False))
        self.assertEqual((len(packets), stats, incomplete), (16, [], None))
        with self.assertRaises(rc.CaptureError):
            rc.read_pcapng(b"\x00" * 64)

    def test_rejects_other_link_types(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "x.pcapng"
            path.write_bytes(capture_bytes(link=195))
            with self.assertRaisesRegex(rc.CaptureError, "283"):
                rc.analyse(path, tshark="/nonexistent/tshark")


@unittest.skipUnless(TSHARK, "tshark not available (set TSHARK=...)")
class Dissection(unittest.TestCase):
    def analyse(self, data, **kw):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "nrf802154-ch15.pcapng"
            path.write_bytes(data)
            return rc.analyse(path, tshark=TSHARK, salt="test", **kw)

    def test_frames_acks_retries(self):
        r = self.analyse(capture_bytes(), device_short=0x1234)
        f = {x["index"]: x for x in r["frames"]}
        self.assertEqual([f[i]["kind"] for i in (1, 3, 5, 16)],
                         ["association_request", "data_request", "association_response", "beacon"])
        self.assertEqual(r["channels"], [15])
        self.assertEqual(f[1]["phr_length"], 21)
        self.assertEqual((f[8]["src16"], f[8]["dst16"], f[8]["dst_pan"]), ("0x1234", "0x0000", "0x1a62"))
        self.assertEqual(f[1]["ack"], {"status": "PAIRED", "class": "INFERRED", "candidates": [2]})
        self.assertEqual(f[3]["ack"]["status"], "PAIRED")
        self.assertEqual(f[5]["ack"]["status"], "NO_ACK_OBSERVED")
        self.assertEqual(f[6]["ack"]["candidates"], [7])
        self.assertEqual([f[i]["ack"]["status"] for i in (8, 9, 10)], ["AMBIGUOUS"] * 3)
        self.assertEqual(f[10]["ack"]["candidates"], [8, 9])
        self.assertEqual(f[11]["ack"], {"status": "NO_ACK_OBSERVED", "class": "UNKNOWN", "candidates": []})
        self.assertEqual(f[12]["ack"], {"status": "NOT_REQUESTED"})
        self.assertEqual(f[13]["ack"]["status"], "UNPAIRED_ACK")
        self.assertEqual(r["ack"]["unpaired_acks"], 1)
        self.assertEqual([g["frames"] for g in r["retry_candidates"]], [[5, 6]])
        self.assertEqual([(x["first"], x["second"]) for x in r["dsn_reuse"]], [(14, 15)])
        self.assertEqual(r["device"], {"short": "0x1234", "addressed": 0, "sent": 5})

    def test_metadata_privacy_and_limits(self):
        r = self.analyse(capture_bytes())
        f = {x["index"]: x for x in r["frames"]}
        self.assertEqual((f[16]["rss_dbm"], f[16]["lqi"], f[1]["rss_dbm"], f[1]["lqi"]), (None, None, -60.0, 200))
        self.assertEqual(r["limitations"]["missing_metadata"], {"rss_dbm": 1, "lqi": 1, "channel": 0})
        self.assertTrue(r["limitations"]["fcs"].startswith("UNKNOWN"))
        self.assertTrue(all(x["fcs"].startswith("UNKNOWN") for x in r["frames"]))
        self.assertEqual(r["capture"]["statistics"]["received"], 16)
        self.assertIn("not CC2530 time", r["clock"]["domain"])
        text = repr(r).lower()
        for secret in ("00:12:4b:00:01:02:03:04", "00124b0001020304", "00:12:4b:00:aa:bb:cc:dd"):
            self.assertNotIn(secret, text)
            self.assertNotIn(secret, text.replace(":", ""))
        self.assertEqual(f[1]["src64_hash"], self.analyse(capture_bytes())["frames"][0]["src64_hash"])
        self.assertEqual(len(f[1]["src64_hash"]), 12)

    def test_unclosed_capture_is_reported(self):
        r = self.analyse(capture_bytes(close=False))
        self.assertIsNone(r["capture"]["statistics"])
        self.assertIn("not closed cleanly", r["capture"]["statistics_note"])

    def test_cut_short_capture(self):
        cut = capture_bytes(close=False)[:-10]  # no ISB and the last EPB cut
        r = self.analyse(cut)
        self.assertEqual(r["capture"]["packets"], 15)
        self.assertIsNotNone(r["capture"]["incomplete"])

    def test_cc2530_header_matches_without_clock_alignment(self):
        diag = {"last_good_mac_header": {"fcf": "0x8000", "dsn": 5, "phr_length": 13},
                "first_overflow": {"status": "PROVEN", "last_good_before":
                                   {"fcf": "0xcc63", "dsn": 0x80, "phr_length": 27}}}
        m = self.analyse(capture_bytes(), rx_diag=diag, after_ms=50)["cc2530_match"]
        self.assertEqual((m["last_good_mac_header"]["status"], m["last_good_mac_header"]["candidates"]),
                         ("UNIQUE_CONTENT", [16]))
        first = m["first_overflow.last_good_before"]
        self.assertEqual((first["status"], first["candidates"], first["distinct_contents"]),
                         ("UNIQUE_CONTENT", [5, 6], 1))
        self.assertEqual([x["index"] for x in first["air_after_last_candidate"]], [7, 8, 9, 10, 11, 12])
        miss = self.analyse(capture_bytes(), rx_diag={"last_good_mac_header": {"fcf": "0x8841", "dsn": 0x55,
                                                                                "phr_length": 12}})
        self.assertEqual(miss["cc2530_match"]["last_good_mac_header"]["status"], "NOT_IN_CAPTURE")
        self.assertEqual(miss["cc2530_match"]["first_overflow.last_good_before"]["status"], "NOT_RECORDED_BY_CC2530")
        both = self.analyse(capture_bytes(), rx_diag={"last_good_mac_header": {"fcf": "0x8861", "dsn": 0x84,
                                                                                "phr_length": 12}})
        self.assertEqual(both["cc2530_match"]["last_good_mac_header"]["status"], "AMBIGUOUS")

    def test_empty_capture(self):
        r = self.analyse(pcapng([]))
        self.assertEqual((r["capture"]["packets"], r["counts"], r["retry_candidates"]), (0, {}, []))


if __name__ == "__main__":
    unittest.main()
