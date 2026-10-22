import copy
import unittest
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

import llvm_hw_acceptance as v

ZONE = ZoneInfo("UTC")
T0 = datetime(2030, 1, 1, 12, 0, 0, tzinfo=ZONE)
IEEE, OTHER, NWK = "02:00:00:00:00:00:00:01", "02:00:00:00:00:00:00:02", 0x1234
DETAIL = dict(nwk=NWK, model="LG-ESL29", manufacturer="cc2530-zigbee", signature=dict(endpoints={"1": dict(
    profile_id="0x0104", device_type="0xffff", output_clusters=[],
    input_clusters=["0x0000", "0x0001", "0x0003", "0x0402", "0x0405"])}))
READS = dict(temperature_raw=2110, humidity_raw=4525, battery_voltage_raw=30, battery_percentage_raw=199)
TC = {"UNSECURED": ("STANDARD_SECURITY_UNSECURED_JOIN", 1, "USE_PRECONFIGURED_KEY", 0),
      "SECURED_REJOIN": ("STANDARD_SECURITY_SECURED_REJOIN", 0, "NO_ACTION", 3),
      "LEFT": ("DEVICE_LEFT", 2, "NO_ACTION", 3)}


class Log:
    """Synthetic HA core log in the observed bellows/zigpy record shapes."""

    def __init__(self, base=T0):
        self.rows, self.base = [], base

    def add(self, seconds, logger, message, level="DEBUG"):
        ts = self.base + timedelta(seconds=seconds)
        self.rows.append((ts, f"{ts:%Y-%m-%d %H:%M:%S}.{ts.microsecond // 1000:03d} {level} (MainThread) "
                              f"[{logger}] {message}"))
        return self

    def text(self):
        return "\n".join(row for _, row in sorted(self.rows, key=lambda r: r[0])) + "\n"

    def records(self):
        return v.parse_log(self.text(), ZONE)

    def callback(self, s, kind, protocol, echo, echo_delay=0.001, with_echo=True):
        self.add(s, v.PROTOCOL, f"Received command {kind}: {{{protocol}}}")
        if with_echo:
            self.add(s + echo_delay, v.APPLICATION, f"Received {kind} frame with [{echo}]")
        return self

    def tc(self, s, kind="UNSECURED", ieee=IEEE, nwk=NWK, zigpy=True, new=True, **kw):
        status, code, decision, dcode = TC[kind]
        self.callback(s, "trustCenterJoinHandler",
                      f"'newNodeId': 0x{nwk:04X}, 'newNodeEui64': {ieee}, 'status': <EmberDeviceUpdate.{status}: "
                      f"{code}>, 'policyDecision': <EmberJoinDecision.{decision}: {dcode}>, 'parentOfNewNodeId': 0x0000",
                      f"0x{nwk:04X}, {ieee}, <EmberDeviceUpdate.{status}: {code}>, <EmberJoinDecision.{decision}: "
                      f"{dcode}>, 0x0000", **kw)
        if zigpy:
            verb = "left" if kind == "LEFT" else "joined"
            self.add(s + 0.002, "zigpy.application", f"{'New device' if new else 'Device'} 0x{nwk:04x} ({ieee}) "
                                                    f"{verb} the network", "INFO")
        return self

    def child(self, s, joining=True, ieee=IEEE, nwk=NWK):
        flag = "<Bool.true: 1>" if joining else "<Bool.false: 0>"
        return self.callback(s, "childJoinHandler",
                             f"'index': 0, 'joining': {flag}, 'childId': 0x{nwk:04X}, 'childEui64': {ieee}, "
                             f"'childType': <EmberNodeType.END_DEVICE: 3>",
                             f"0, {flag}, 0x{nwk:04X}, {ieee}, <EmberNodeType.END_DEVICE: 3>")

    def announce(self, s, ieee=IEEE, nwk=NWK):
        """ZDO Device_annce: zigpy handle_join logs 'Device .. joined' (not the child join)."""
        self.add(s, "zigpy.application", f"Device 0x{nwk:04x} ({ieee}) joined the network", "INFO")
        return self.add(s + 0.002, "zigpy.application", f"Received frame on uninitialized device <Device "
                        f"model=None manuf=None nwk=0x{nwk:04X} ieee={ieee} is_initialized=False> from ep 0 to "
                        "ep 0, cluster 19: Serialized[b'\\x00']")

    def key(self, s, status="TC_RESPONDED_TO_KEY_REQUEST", code=6, ieee=IEEE, **kw):
        return self.callback(s, "zigbeeKeyEstablishmentHandler",
                             f"'partner': {ieee}, 'status': <EmberKeyStatus.{status}: {code}>",
                             f"{ieee}, <EmberKeyStatus.{status}: {code}>", **kw)

    def zcl(self, s, cluster, body, nwk=NWK):
        return self.add(s, "zigpy.zcl", f"[0x{nwk:04X}:1:0x{cluster:04X}] {body}")

    def bind(self, s, tsn, cluster, status=0, nwk=NWK):
        data = bytes([tsn]) + bytes.fromhex(IEEE.replace(":", ""))[::-1] + bytes([1]) + cluster.to_bytes(2, "little") \
            + bytes([3]) + bytes(8) + bytes([1])
        self.add(s, v.APPLICATION, "Sending packet ZigbeePacket(src=AddrModeAddress(addr_mode=<AddrMode.NWK: 2>, "
                 f"address=0x0000), src_ep=0, dst=AddrModeAddress(addr_mode=<AddrMode.NWK: 2>, address=0x{nwk:04X}), "
                 f"dst_ep=0, tsn={tsn}, profile_id=0, cluster_id=<ZDOCmd.Bind_req: 33>, data=Serialized[{data!r}], "
                 "radius=0)")
        if status is not None:
            self.add(s + 0.3, "zigpy.application", "Received a packet: ZigbeePacket(src=AddrModeAddress(addr_mode="
                     f"<AddrMode.NWK: 2>, address=0x{nwk:04X}), src_ep=0, dst=AddrModeAddress(addr_mode=<AddrMode.NWK:"
                     f" 2>, address=0x0000), dst_ep=0, tsn=99, profile_id=0, cluster_id=32801, "
                     f"data=Serialized[{bytes([tsn, status])!r}], lqi=132)")
        return self

    def configure(self, s, cluster, status="SUCCESS"):
        code = 0 if status == "SUCCESS" else 0x86
        return self.zcl(s, cluster, f"Decoded ZCL frame: X:Configure_Reporting_rsp(status_records=["
                                    f"ConfigureReportingResponseRecord(status=<Status.{status}: {code}>)])")

    def report(self, s, cluster, tsn, value, duplicate=False):
        body = (f"Received command 0x0A (TSN {tsn}): Report_Attributes(attribute_reports=[Attribute("
                f"attrid=0x0000, value=TypeValue(type=int16s, value={value}))])")
        self.zcl(s, cluster, body)
        return self.zcl(s + 0.5, cluster, body) if duplicate else self

    def identify(self, s, timeout=49):
        self.zcl(s, 3, "Sending request: identify(identify_time=60)")
        self.zcl(s + 10, 3, "Sending request: identify_query()")
        return self.zcl(s + 10.3, 3, f"Decoded ZCL frame: Identify:identify_query_response(timeout={timeout})")

    def filler(self, s):
        return self.add(s, "homeassistant.core", "Bus:Handling <Event state_changed>")


def h1_like(log=None, keys=True):
    """Normal successful commissioning in H1 order (all EZSP callbacks logged twice)."""
    log = log or Log()
    log.filler(0).tc(5).child(7.5).announce(7.51)
    if keys:
        log.key(10).key(14, "TC_REQUESTER_VERIFY_KEY_SUCCESS", 52)
    log.add(43, "zigpy.application", f"Device is initialized <Device model='LG-ESL29' manuf='cc2530-zigbee' "
                                     f"nwk=0x{NWK:04X} ieee={IEEE} is_initialized=True>")
    for i, cluster in enumerate((0x0001, 0x0402, 0x0405)):
        log.bind(44 + i, 6 + 2 * i, cluster).configure(44.7 + i, cluster)
    log.identify(83).report(270, 0x0405, 1, 4600).report(337, 0x0402, 2, 2150).filler(420)
    return log


def commission(log, **kw):
    return v.commissioning(log.records(), IEEE, T0 + timedelta(seconds=1), T0 + timedelta(seconds=400),
                           kw.pop("detail", DETAIL), READS, kw.pop("identify", 59))


FIRMWARE = dict(
    status=dict(phase=5, reason=0, saw_ready=1, bdb_phase=13, bdb_result=0, member=1, radio_errors=0, steps=128252),
    bdb=dict(phase=13, result=0, member=1, got_tc=1, got_confirm=1),
    security_keys=dict(phase=8, result=9),
    driver=dict(fault=0, now=26236350, rx_serial=5968, gaps=50, dropped=0),
    adapter=dict(fault=0, lossy=0), radio=dict(errors=0), flash=dict(fault=0, result=0),
    stack=dict(within_admitted=True, static_limit_sp=0xE9, sampled_sp=0x9A, nonzero_watermark_address=0xE4,
               watermark_depth_lower_bound=101),
    fmap=2, pc_location=dict(symbol="nwk_aps_step", bank=2, offset=175, within_size=True),
    at_fault_thunk=False, halted_by_firmware=False, elapsed_seconds=420.4)


def firmware(**changes):
    value = copy.deepcopy(FIRMWARE)
    for path, item in changes.items():
        target = value
        *parents, leaf = path.split("__")
        for p in parents:
            target = target[p]
        target[leaf] = item
    return v.firmware(value)


class DuplicateLogging(unittest.TestCase):
    def test_same_event_from_both_loggers_is_one_event(self):
        events, stats = v.callbacks(Log().tc(1).records())
        self.assertEqual(len(events), 1)
        self.assertEqual(stats, dict(protocol_records=1, application_records=1, semantic_events=1))

    def test_two_genuine_joins_of_two_nodes_count_two(self):
        log = Log().tc(1).tc(1.0005, ieee=OTHER, nwk=0x2222)
        events, _ = v.callbacks(log.records())
        self.assertEqual(len(events), 2)

    def test_identical_callbacks_in_the_same_millisecond_stay_two(self):
        log = Log()
        for _ in range(2):
            log.add(1, v.PROTOCOL, "Received command trustCenterJoinHandler: {'newNodeId': 0x1234, 'newNodeEui64': "
                    f"{IEEE}, 'status': <EmberDeviceUpdate.STANDARD_SECURITY_UNSECURED_JOIN: 1>, 'policyDecision': "
                    "<EmberJoinDecision.USE_PRECONFIGURED_KEY: 0>, 'parentOfNewNodeId': 0x0000}")
        for _ in range(2):
            log.add(1, v.APPLICATION, f"Received trustCenterJoinHandler frame with [0x1234, {IEEE}, <EmberDeviceUpdate."
                    "STANDARD_SECURITY_UNSECURED_JOIN: 1>, <EmberJoinDecision.USE_PRECONFIGURED_KEY: 0>, 0x0000]")
        self.assertEqual(len(v.callbacks(log.records())[0]), 2)

    def test_same_node_joining_twice_is_association_plus_rejoin(self):
        m = v.membership(Log().tc(1).child(3).tc(60, new=False).child(62).records(), IEEE)
        self.assertEqual((len(m["associations"]), len(m["rejoins"]), len(m["child_joins"])), (1, 1, 2))

    def test_join_then_leave_and_rejoin_are_distinct_events(self):
        log = Log().tc(1).child(3).announce(3.1).tc(100, "LEFT").child(100.5, joining=False)
        log.tc(130, "SECURED_REJOIN", new=False).child(131).announce(131.1)
        m = v.membership(log.records(), IEEE)
        self.assertEqual(len(m["associations"]), 1)
        self.assertEqual([r["status"] for r in m["rejoins"]], ["STANDARD_SECURITY_SECURED_REJOIN"])
        self.assertEqual([x["representations"] for x in m["leaves"]], [["DEVICE_LEFT", "ZIGPY_LEFT", "CHILD_LEFT"]])
        self.assertEqual(len(m["announces"]), 2)

    def test_one_physical_leave_in_three_representations_is_one_leave(self):
        log = Log().tc(1).child(3).child(50, joining=False).tc(50.006, "LEFT")
        m = v.membership(log.records(), IEEE)
        self.assertEqual(len(m["leaves"]), 1)
        self.assertEqual(sorted(m["leaves"][0]["representations"]), ["CHILD_LEFT", "DEVICE_LEFT", "ZIGPY_LEFT"])

    def test_two_separate_leaves_stay_two(self):
        log = Log().tc(1).child(3).tc(50, "LEFT").tc(51, "SECURED_REJOIN", new=False).tc(51.5, "LEFT")
        self.assertEqual(len(v.membership(log.records(), IEEE)["leaves"]), 2)

    def test_child_join_without_announce_has_no_zigpy_join(self):
        m = v.membership(Log().tc(1).child(3).records(), IEEE)
        self.assertEqual((len(m["associations"]), len(m["child_joins"]), len(m["announces"])), (1, 1, 0))

    def test_duplicated_key_records_are_one_exchange(self):
        log = Log().tc(1).child(3).key(5).key(9, "TC_REQUESTER_VERIFY_KEY_SUCCESS", 52)
        m = v.membership(log.records(), IEEE)
        self.assertEqual([k["code"] for k in m["keys"]], [6, 52])

    def test_echo_without_protocol_record_is_ambiguous(self):
        log = Log().add(1, v.APPLICATION, f"Received zigbeeKeyEstablishmentHandler frame with [{IEEE}, "
                                          "<EmberKeyStatus.TC_RESPONDED_TO_KEY_REQUEST: 6>]")
        with self.assertRaises(v.Ambiguous):
            v.callbacks(log.records())

    def test_protocol_record_without_echo_is_ambiguous_when_echo_logger_active(self):
        log = Log().tc(1).key(5, with_echo=False)
        with self.assertRaises(v.Ambiguous):
            v.callbacks(log.records())

    def test_echo_far_from_protocol_record_is_not_paired(self):
        with self.assertRaises(v.Ambiguous):
            v.callbacks(Log().tc(1, echo_delay=5).records())

    def test_unknown_join_status_is_ambiguous(self):
        log = Log().tc(1)
        log.rows = [(t, r.replace("STANDARD_SECURITY_UNSECURED_JOIN: 1", "SOMETHING_NEW: 9")) for t, r in log.rows]
        with self.assertRaises(v.Ambiguous):
            v.membership(log.records(), IEEE)

    def test_trust_center_join_without_zigpy_record_is_ambiguous(self):
        with self.assertRaises(v.Ambiguous):
            v.membership(Log().tc(1, zigpy=False).child(3).records(), IEEE)


class Commissioning(unittest.TestCase):
    def test_h1_like_run_passes(self):
        verdict = commission(h1_like())
        self.assertTrue(verdict["passed"], verdict["checks"])
        self.assertEqual(verdict["evidence"]["nwk"], NWK)

    def test_not_tied_to_h1_timestamps_or_addresses(self):
        log = h1_like(Log(T0 + timedelta(days=3, seconds=17)))
        start = T0 + timedelta(days=3, seconds=18)
        verdict = v.commissioning(log.records(), IEEE, start, start + timedelta(seconds=399), DETAIL, READS, 59)
        self.assertTrue(verdict["passed"], verdict["checks"])

    def test_missing_verify_key_success_fails(self):
        log = h1_like(keys=False).key(10)
        self.assertEqual(commission(log)["checks"]["authenticated_key_exchange"], v.FAIL)

    def test_key_failure_status_fails(self):
        log = h1_like().key(16, "TC_REQUESTER_VERIFY_KEY_FAILURE", 53)
        self.assertEqual(commission(log)["checks"]["authenticated_key_exchange"], v.FAIL)

    def test_key_before_association_fails(self):
        log = h1_like().key(2)
        self.assertEqual(commission(log)["checks"]["authenticated_key_exchange"], v.FAIL)

    def test_interview_before_key_verification_fails(self):
        log = h1_like(keys=False).key(10).key(50, "TC_REQUESTER_VERIFY_KEY_SUCCESS", 52)
        self.assertEqual(commission(log)["checks"]["interview"], v.FAIL)

    def test_leave_and_rejoin_after_commissioning_fails(self):
        log = h1_like().tc(200, "LEFT", new=False).tc(230, "SECURED_REJOIN", new=False)
        checks = commission(log)["checks"]
        self.assertEqual(checks["no_leave_or_rejoin"], v.FAIL)

    def test_second_unsecured_association_fails(self):
        log = h1_like().tc(200, new=False).child(202)
        checks = commission(log)["checks"]
        self.assertEqual((checks["association"], checks["no_leave_or_rejoin"]), (v.FAIL, v.FAIL))

    def test_join_then_firmware_abandon_before_key_exchange_fails(self):
        """H2 shape: association + child join, no announce, no TC link key, orderly leave."""
        log = Log().filler(0).tc(5).child(7.5).child(12.7, joining=False).tc(12.706, "LEFT").filler(420)
        verdict = commission(log)
        self.assertFalse(verdict["passed"])
        for name in ("authenticated_key_exchange", "no_leave_or_rejoin", "interview"):
            self.assertEqual(verdict["checks"][name], v.FAIL, name)

    def test_failed_bind_fails(self):
        log = h1_like().bind(60, 20, 0x0402, status=0x8C)
        self.assertEqual(commission(log)["checks"]["bind"], v.FAIL)

    def test_missing_report_fails(self):
        log = h1_like()
        log.rows = [row for row in log.rows if "TSN 2)" not in row[1]]
        self.assertEqual(commission(log)["checks"]["temperature"], v.FAIL)

    def test_identify_query_without_response_fails(self):
        log = h1_like()
        log.rows = [row for row in log.rows if "identify_query_response" not in row[1]]
        self.assertEqual(commission(log)["checks"]["identify_query"], v.FAIL)

    def test_capture_not_covering_window_is_ambiguous(self):
        log = h1_like()
        log.rows = [row for row in log.rows if row[0] < T0 + timedelta(seconds=300)]
        verdict = commission(log)
        self.assertFalse(verdict["passed"])
        self.assertEqual(verdict["checks"]["evidence"], v.AMBIGUOUS)

    def test_nwk_disagreement_is_ambiguous(self):
        verdict = commission(h1_like(), detail=dict(DETAIL, nwk=0x4321))
        self.assertFalse(verdict["passed"])
        self.assertEqual(verdict["checks"]["nwk_consistent"], v.AMBIGUOUS)

    def test_wrong_cluster_set_fails(self):
        detail = copy.deepcopy(DETAIL)
        detail["signature"]["endpoints"]["1"]["input_clusters"].pop()
        self.assertEqual(commission(h1_like(), detail=detail)["checks"]["clusters"], v.FAIL)


class Firmware(unittest.TestCase):
    def test_commissioned_then_link_status_rejected_passes(self):
        verdict = firmware()
        self.assertTrue(verdict["passed"], verdict["checks"])
        self.assertEqual(verdict["evidence"]["security_last_result_name"], "IDENTITY")

    def test_benign_per_frame_results_are_observations(self):
        for result in (0, 1, 3, 4, 7, 8, 9, 10, 11, 12, 13):
            self.assertTrue(firmware(security_keys__result=result)["passed"], result)

    def test_firmware_fatal_security_results_fail(self):
        for result in v.SECURITY_KEYS_FATAL_RESULTS:
            self.assertEqual(firmware(security_keys__result=result)["checks"]["security_last_result_not_fatal"],
                             v.FAIL)
        self.assertFalse(firmware(security_keys__result=200)["passed"])

    def test_security_keys_not_verified_fails(self):
        for phase in (0, 7, 9, 10, 11):
            self.assertEqual(firmware(security_keys__phase=phase)["checks"]["security_keys_verified_state"], v.FAIL)

    def test_bdb_result_nonzero_fails(self):
        self.assertEqual(firmware(bdb__result=3)["checks"]["bdb_ready"], v.FAIL)
        self.assertEqual(firmware(status__bdb_result=3)["checks"]["bdb_ready"], v.FAIL)

    def test_gate_not_ready_fails(self):
        self.assertFalse(firmware(status__saw_ready=0)["passed"])
        self.assertFalse(firmware(status__reason=4)["passed"])

    def test_faults_fail(self):
        for change in (dict(driver__fault=1), dict(adapter__fault=2), dict(radio__errors=1), dict(flash__fault=1),
                       dict(at_fault_thunk=True), dict(halted_by_firmware=True), dict(fmap=0),
                       dict(pc_location__within_size=False), dict(stack__nonzero_watermark_address=0xEA),
                       dict(stack__within_admitted=False)):
            self.assertFalse(firmware(**change)["passed"], change)

    def test_reset_detected_by_symbol_clock(self):
        self.assertEqual(firmware(driver__now=62500 * 100)["checks"]["no_reset_clock"], v.FAIL)
        self.assertEqual(firmware(driver__now=62500 * 3300, elapsed_seconds=3310.0)["checks"]["no_reset_clock"],
                         v.PASS)


class Captures(unittest.TestCase):
    def test_overlapping_captures_merge_without_duplicates(self):
        text = h1_like().text().splitlines()
        merged = v.merge_captures(["\n".join(text[:30]), "\n".join(text[10:])], ZONE)
        self.assertEqual(len(merged), len(text))

    def test_captures_merge_when_later_capture_is_short(self):
        text = h1_like().text().splitlines()
        self.assertEqual(len(v.merge_captures(["\n".join(text[:20]), "\n".join(text[15:22])], ZONE, overlap=5)), 22)

    def test_insufficient_overlap_is_ambiguous(self):
        text = h1_like().text().splitlines()
        with self.assertRaises(v.Ambiguous):
            v.merge_captures(["\n".join(text[:20]), "\n".join(text[18:])], ZONE)

    def test_non_overlapping_captures_are_ambiguous(self):
        text = h1_like().text().splitlines()
        with self.assertRaises(v.Ambiguous):
            v.merge_captures(["\n".join(text[:30]), "\n".join(text[31:])], ZONE)


def soak_log(report_times, extra=None):
    log = h1_like()
    for i, s in enumerate(report_times):
        log.report(s, 0x0402, 10 + i, 2100 + i).report(s + 3, 0x0405, 50 + i, 4500 + i, duplicate=True)
    if extra:
        extra(log)
    log.identify(2900).filler(3000)
    return log


def soak(log, reads=None):
    reads = reads if reads is not None else [dict(ok=True, values=READS)] * 7
    return v.steady_state(log.records(), IEEE, NWK, T0 + timedelta(seconds=400), T0 + timedelta(seconds=2950),
                          reads, 58)


class Soak(unittest.TestCase):
    def test_regular_reporting_passes(self):
        verdict = soak(soak_log([1200, 2100, 2940]))
        self.assertTrue(verdict["passed"], verdict["checks"])
        self.assertEqual(verdict["evidence"]["cadence"]["0x0405"]["reports"], 3)

    def test_one_lost_report_is_an_observation(self):
        verdict = soak(soak_log([1200, 2900]))
        self.assertTrue(verdict["passed"], verdict["checks"])
        self.assertTrue(verdict["evidence"]["observations"])

    def test_reporting_stall_fails(self):
        verdict = soak(soak_log([450, 600]))
        self.assertEqual(verdict["checks"]["reporting_continues"], v.FAIL)

    def test_leave_rejoin_during_soak_fails(self):
        verdict = soak(soak_log([1200, 2100, 2940], lambda log: log.tc(1500, "SECURED_REJOIN", new=False)))
        self.assertEqual(verdict["checks"]["no_association_rejoin_or_leave"], v.FAIL)

    def test_key_reestablishment_during_soak_fails(self):
        verdict = soak(soak_log([1200, 2100, 2940], lambda log: log.key(1500)))
        self.assertEqual(verdict["checks"]["no_key_reestablishment"], v.FAIL)

    def test_reannounce_during_soak_fails(self):
        verdict = soak(soak_log([1200, 2100, 2940], lambda log: log.announce(1500)))
        self.assertEqual(verdict["checks"]["no_association_rejoin_or_leave"], v.FAIL)

    def test_single_failed_read_round_is_an_observation(self):
        good, bad = dict(ok=True, values=READS), dict(ok=False)
        verdict = soak(soak_log([1200, 2100, 2940]), reads=[good, bad, good, good])
        self.assertEqual(verdict["checks"]["periodic_reads"], v.PASS)
        self.assertIn("read round 1 failed", verdict["evidence"]["observations"])

    def test_consecutive_or_final_read_failures_fail(self):
        good, bad = dict(ok=True, values=READS), dict(ok=False)
        for reads in ([good, bad, bad, good], [good, good, bad], [], [dict(ok=True, values=dict(READS, humidity_raw=-1)),
                                                                   good]):
            self.assertEqual(soak(soak_log([1200, 2100, 2940]), reads=reads)["checks"]["periodic_reads"], v.FAIL)

    def test_post_soak_identify_required(self):
        log = soak_log([1200, 2100, 2940])
        log.rows = [row for row in log.rows if not (row[0] > T0 + timedelta(seconds=2800) and "0x0003]" in row[1])]
        self.assertEqual(soak(log)["checks"]["post_identify_query"], v.FAIL)


if __name__ == "__main__":
    unittest.main()
