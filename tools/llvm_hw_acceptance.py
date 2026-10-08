"""Semantic hardware-acceptance verifier for the CC2530 LLVM-MCS51 study.

Pure functions over (a) Home Assistant core-log text captured around a run and
(b) the decoded terminal firmware snapshot. No hardware, HA or file access.

Rules (see docs/LLVM_MCS51_PORT_STUDY.md, "Hardware acceptance verifier
correction"):

* Count semantic Zigbee events, not log records. bellows logs every EZSP
  callback once as ``[bellows.ezsp.protocol] Received command X: {..}`` and
  once as ``[bellows.zigbee.application] Received X frame with [..]``. An
  application echo is paired 1:1 with the earliest unpaired protocol record of
  identical payload no more than ECHO_WINDOW later; two genuine identical
  callbacks therefore stay two events.
* Commissioning is judged at its own checkpoint (association -> TC key
  exchange -> interview) and from latched/state firmware facts. The
  security-keys *last-operation* result is a steady-state observation; only
  results the firmware itself treats as fatal can fail acceptance.
* Association, rejoin and leave are distinct semantic events.
* Anything ambiguous (unpaired echo, unknown status, capture gap, NWK
  mismatch) is AMBIGUOUS, which never passes.
"""
from dataclasses import dataclass, field
from datetime import datetime, timedelta
import ast
import re

PASS, FAIL, AMBIGUOUS = "PASS", "FAIL", "AMBIGUOUS"
ECHO_WINDOW = timedelta(seconds=1)
PROTOCOL, APPLICATION = "bellows.ezsp.protocol", "bellows.zigbee.application"
CALLBACK_FIELDS = {"trustCenterJoinHandler": 5, "childJoinHandler": 5, "zigbeeKeyEstablishmentHandler": 2}
ASSOCIATION = "STANDARD_SECURITY_UNSECURED_JOIN"
REJOIN = {"STANDARD_SECURITY_SECURED_REJOIN", "STANDARD_SECURITY_UNSECURED_REJOIN"}
LEFT = "DEVICE_LEFT"
KEY_RESPONDED, KEY_VERIFIED = "TC_RESPONDED_TO_KEY_REQUEST", "TC_REQUESTER_VERIFY_KEY_SUCCESS"
KEY_FAILURE = re.compile(r"FAIL|TIMEOUT|ERROR|NOT_|INVALID|DENIED")

# include/security_keys.h and include/join_smoke.h / bdb_join.h (frozen tree).
SECURITY_KEYS_VERIFIED = 8
SECURITY_KEYS_FATAL_RESULTS = {5: "STORAGE", 6: "CRYPTO", 14: "EXHAUSTED"}  # nwk_aps.c drops ready
SECURITY_KEYS_RESULTS = ("OK", "EMPTY", "ARGUMENT", "STATE", "FORMAT", "STORAGE", "CRYPTO", "AUTH",
                         "REPLAY", "IDENTITY", "SELECTOR", "CONTEXT", "UNSUPPORTED", "SPACE", "EXHAUSTED")
JS_READY, BDB_READY = 5, 13


@dataclass(frozen=True)
class Profile:
    model: str = "LG-ESL29"
    manufacturer: str = "cc2530-zigbee"
    endpoint: int = 1
    profile_id: int = 0x0104
    device_type: int = 0xFFFF
    input_clusters: tuple = (0x0000, 0x0001, 0x0003, 0x0402, 0x0405)
    bind_clusters: tuple = (0x0001, 0x0402, 0x0405)
    report_max_interval: tuple = ((0x0402, 900), (0x0405, 900))
    report_tolerance_s: int = 120


class Ambiguous(Exception):
    pass


@dataclass(frozen=True)
class Record:
    ts: datetime
    level: str
    logger: str
    message: str
    seq: int


@dataclass
class Event:
    kind: str
    ts: datetime
    fields: tuple
    seq: int
    echo: int = None
    extra: dict = field(default_factory=dict)


LINE = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) (\w+) \(([^)]*)\) \[([\w.]+)\] (.*)$")


def parse_log(text, zone):
    """HA core log -> Records; continuation lines join the preceding record."""
    rows = []
    for line in text.splitlines():
        m = LINE.match(line)
        if m:
            rows.append([datetime.fromisoformat(m[1]).replace(tzinfo=zone), m[2], m[4], m[5]])
        elif rows:
            rows[-1][3] += "\n" + line
    return [Record(ts, level, logger, message, i) for i, (ts, level, logger, message) in enumerate(rows)]


def merge_captures(texts, zone, overlap=20):
    """Merge successive captures of one journal: the earlier capture's suffix must equal the
    later capture's prefix over at least ``overlap`` records (or the whole shorter capture);
    otherwise records may be missing and the result is ambiguous."""
    merged = []
    for text in texts:
        records = parse_log(text, zone)
        if not records:
            raise Ambiguous("empty log capture")
        body = [(r.ts, r.level, r.logger, r.message) for r in records]
        if not merged:
            merged = body
            continue
        need = min(overlap, len(body), len(merged))
        for pos in range(len(merged)):
            n = len(merged) - pos
            if merged[pos] == body[0] and n >= need and merged[pos:pos + min(n, len(body))] == body[:n]:
                merged.extend(body[n:])
                break
        else:
            raise Ambiguous("successive log captures do not overlap")
    return [Record(ts, level, logger, message, i) for i, (ts, level, logger, message) in enumerate(merged)]


def covered(records, start, end):
    if not records or records[0].ts > start or records[-1].ts < end:
        raise Ambiguous("log capture does not cover the evaluated window")


ATOM = re.compile(r"<[\w.]+: -?\d+>|(?:[0-9a-fA-F]{2}:){7}[0-9a-fA-F]{2}|0x[0-9a-fA-F]+|-?\d+|True|False|None")
ENUM = re.compile(r"<([\w]+)\.([\w]+): (-?\d+)>")


def atoms(payload):
    out = []
    for token in ATOM.findall(re.sub(r"'\w+':\s*", "", payload)):
        enum = ENUM.fullmatch(token)
        if enum:
            out.append((enum[2], int(enum[3])))
        elif ":" in token:
            out.append(token.lower())
        elif token.startswith("0x"):
            out.append(int(token, 16))
        elif token in ("True", "False", "None"):
            out.append(token)
        else:
            out.append(int(token))
    return tuple(out)


def callbacks(records):
    """Semantic EZSP callback events with 1:1 echo pairing; fail closed on doubt."""
    protocol, echoes = [], []
    for r in records:
        if r.logger == PROTOCOL:
            m = re.match(r"Received command (\w+): (\{.*\})$", r.message, re.S)
            target = protocol
        elif r.logger == APPLICATION:
            m = re.match(r"Received (\w+) frame with (\[.*\])$", r.message, re.S)
            target = echoes
        else:
            continue
        if not m or m[1] not in CALLBACK_FIELDS:
            continue
        values = atoms(m[2])
        if len(values) != CALLBACK_FIELDS[m[1]]:
            raise Ambiguous(f"unparsed {m[1]} payload")
        target.append(Event(m[1], r.ts, values, r.seq))
    for echo in echoes:
        match = [p for p in protocol if p.echo is None and p.kind == echo.kind and p.fields == echo.fields
                 and p.seq < echo.seq and timedelta(0) <= echo.ts - p.ts <= ECHO_WINDOW]
        if not match:
            raise Ambiguous(f"{echo.kind} application record without a protocol record")
        match[0].echo = echo.seq
    if echoes and any(p.echo is None for p in protocol):
        raise Ambiguous("protocol callback without its application record")
    return protocol, dict(protocol_records=len(protocol), application_records=len(echoes),
                          semantic_events=len(protocol))


def zigpy_membership(records, ieee):
    joined, left = [], []
    for r in records:
        if r.logger != "zigpy.application":
            continue
        # zigpy handle_join logs "New device .." for an unknown IEEE and "Device .." otherwise;
        # bellows calls it for every trust-center join and every child join(true).
        m = re.match(r"(?:New device|Device) 0x([0-9a-fA-F]{4}) \(([0-9a-fA-F:]{23})\) (joined|left) the network$",
                     r.message)
        if m and m[2].lower() == ieee:
            (joined if m[3] == "joined" else left).append(dict(ts=r.ts, nwk=int(m[1], 16)))
    return joined, left


def membership(records, ieee):
    """Association / rejoin / leave / key events for one node, in time order."""
    events, dedup = callbacks(records)
    tc = [e for e in events if e.kind == "trustCenterJoinHandler" and e.fields[1] == ieee]
    child = [e for e in events if e.kind == "childJoinHandler" and e.fields[3] == ieee]
    keys = [e for e in events if e.kind == "zigbeeKeyEstablishmentHandler" and e.fields[0] == ieee]
    associations, rejoins, leaves = [], [], []
    for e in tc:
        status = e.fields[2][0] if isinstance(e.fields[2], tuple) else None
        row = dict(ts=e.ts, nwk=e.fields[0], status=status)
        if status == ASSOCIATION:
            (associations if not associations else rejoins).append(row)
        elif status in REJOIN:
            rejoins.append(row)
        elif status == LEFT:
            leaves.append(row)
        else:
            raise Ambiguous(f"unknown trust-center join status {status}")
    child_joins = []
    for e in child:
        joining = e.fields[1][0] if isinstance(e.fields[1], tuple) else None
        if joining == "true":
            child_joins.append(dict(ts=e.ts, nwk=e.fields[2]))
        elif joining == "false":
            leaves.append(dict(ts=e.ts, nwk=e.fields[2], status="CHILD_LEFT"))
        else:
            raise Ambiguous("unknown child-join flag")
    zjoined, zleft = zigpy_membership(records, ieee)
    if len(zjoined) != len(associations) + len(rejoins) + len(child_joins):
        raise Ambiguous("zigpy join records disagree with trust-center/child join events")
    # A zigpy leave without a matching callback (e.g. ZDO leave) is still a leave.
    leaves += [dict(ts=z["ts"], nwk=z["nwk"], status="ZIGPY_LEFT") for z in zleft[len(leaves):]]
    key_events = []
    for e in keys:
        name = e.fields[1][0] if isinstance(e.fields[1], tuple) else None
        if name not in (KEY_RESPONDED, KEY_VERIFIED) and not (name and KEY_FAILURE.search(name)):
            raise Ambiguous(f"unknown key-establishment status {name}")
        key_events.append(dict(ts=e.ts, status=name, code=e.fields[1][1]))
    return dict(associations=associations, rejoins=rejoins, leaves=sorted(leaves, key=lambda x: x["ts"]),
                child_joins=child_joins, keys=key_events, zigpy_joins=len(zjoined), dedup=dedup)


def _serialized(message):
    m = re.search(r"data=Serialized\[(b'(?:[^'\\]|\\.)*'|b\"(?:[^\"\\]|\\.)*\")\]", message)
    return ast.literal_eval(m[1]) if m else None


def application(records, nwk, ieee):
    """Coordinator-side ZDO/ZCL application facts for one node NWK address."""
    dst = f"dst=AddrModeAddress(addr_mode=<AddrMode.NWK: 2>, address=0x{nwk:04X})"
    src = f"src=AddrModeAddress(addr_mode=<AddrMode.NWK: 2>, address=0x{nwk:04X})"
    requests, responses, initialized = {}, {}, []
    configure, reports, identify_query, identify_request = {}, [], [], []
    for r in records:
        msg = r.message
        if r.logger == APPLICATION and msg.startswith("Sending packet") and dst in msg \
                and "cluster_id=<ZDOCmd.Bind_req: 33>" in msg:
            data = _serialized(msg)
            if data is None or len(data) < 12:
                raise Ambiguous("unparsed Bind_req")
            if bytes(data[1:9])[::-1].hex(":") != ieee:
                raise Ambiguous("Bind_req source is not the node")
            requests.setdefault(data[0], dict(ts=r.ts, cluster=int.from_bytes(data[10:12], "little")))
        elif r.logger == "zigpy.application" and msg.startswith("Received a packet") and src in msg \
                and "cluster_id=32801," in msg:
            data = _serialized(msg)
            if data is None or len(data) < 2:
                raise Ambiguous("unparsed Bind_rsp")
            responses.setdefault(data[0], dict(ts=r.ts, status=data[1]))
        elif r.logger == "zigpy.application" and msg.startswith("Device is initialized") \
                and f"nwk=0x{nwk:04X}" in msg and ieee in msg.lower():
            initialized.append(dict(ts=r.ts, model=re.search(r"model='([^']*)'", msg)[1],
                                    manufacturer=re.search(r"manuf='([^']*)'", msg)[1]))
        elif r.logger == "zigpy.zcl" and msg.startswith(f"[0x{nwk:04X}:1:0x"):
            cluster = int(msg[len(f"[0x{nwk:04X}:1:0x"):][:4], 16)
            body = msg.split("] ", 1)[1]
            if ":Configure_Reporting_rsp(" in body:
                statuses = re.findall(r"ConfigureReportingResponseRecord\(status=<Status\.(\w+): \d+>", body)
                if not statuses:
                    raise Ambiguous("unparsed Configure_Reporting_rsp")
                configure.setdefault(cluster, []).append(dict(ts=r.ts, success=set(statuses) == {"SUCCESS"}))
            m = re.match(r"Received command 0x0A \(TSN (\d+)\): Report_Attributes\(", body)
            if m:
                value = re.search(r"value=(-?\d+)\)", body)
                row = dict(ts=r.ts, cluster=cluster, tsn=int(m[1]), value=int(value[1]) if value else None)
                if not any(p["cluster"] == cluster and p["tsn"] == row["tsn"] and p["value"] == row["value"]
                           and row["ts"] - p["ts"] <= timedelta(seconds=5) for p in reports):
                    reports.append(row)
            m = re.search(r"identify_query_response\(timeout=(\d+)\)", body)
            if m and cluster == 3:
                identify_query.append(dict(ts=r.ts, timeout=int(m[1])))
            m = re.match(r"Sending request: identify\(identify_time=(\d+)\)", body)
            if m and cluster == 3:
                identify_request.append(dict(ts=r.ts, time=int(m[1])))
    bind = [dict(tsn=t, **q, status=responses.get(t, {}).get("status"),
                 response_ts=responses.get(t, {}).get("ts")) for t, q in sorted(requests.items())]
    return dict(bind=bind, initialized=initialized, configure=configure, reports=reports,
                identify_query=identify_query, identify_request=identify_request)


def identify_ok(app, active_read):
    """Identify(N) sent, IdentifyTime read back active, and a later Identify Query answered."""
    for request in app["identify_request"]:
        if request["time"] > 0 and 0 < active_read <= request["time"] and any(
                q["timeout"] > 0 and q["ts"] >= request["ts"] for q in app["identify_query"]):
            return True
    return False


def signature_ok(detail, profile):
    eps = detail["signature"]["endpoints"]
    ep = eps.get(str(profile.endpoint)) or eps.get(profile.endpoint)
    if ep is None:
        return False, None
    num = lambda v: int(v, 0) if isinstance(v, str) else int(v)
    summary = dict(profile=num(ep["profile_id"]), device_type=num(ep["device_type"]),
                   input_clusters=sorted(num(v) for v in ep["input_clusters"]),
                   output_clusters=[num(v) for v in ep["output_clusters"]])
    ok = (summary["profile"] == profile.profile_id and summary["device_type"] == profile.device_type
          and summary["input_clusters"] == sorted(profile.input_clusters) and not summary["output_clusters"])
    return ok, summary


def _verdict(checks, evidence):
    return dict(checks=checks, passed=all(v == PASS for v in checks.values()), evidence=evidence)


def commissioning(records, ieee, start, end, detail, reads, identify_active, profile=Profile()):
    """Coordinator view of one commissioning: association -> keys -> interview -> application.

    ``detail``: coordinator device record (nwk, model, manufacturer, signature).
    ``reads``: uncached attribute values returned by the coordinator API.
    ``identify_active``: IdentifyTime read back after the Identify request.
    """
    ieee = ieee.lower()
    checks, ev = {}, {}
    try:
        covered(records, start, end)
        window = [r for r in records if start <= r.ts <= end]
        m = membership(window, ieee)
    except Ambiguous as error:
        return _verdict({"evidence": AMBIGUOUS, "_ambiguity": [str(error)]}, {})
    ev["membership"] = m
    assoc = m["associations"][0] if len(m["associations"]) == 1 else None
    checks["association"] = PASS if assoc and len(m["child_joins"]) == 1 \
        and m["child_joins"][0]["nwk"] == assoc["nwk"] else FAIL
    keys = m["keys"]
    verified = [k for k in keys if k["status"] == KEY_VERIFIED]
    responded = [k for k in keys if k["status"] == KEY_RESPONDED]
    checks["authenticated_key_exchange"] = PASS if (
        assoc and len(verified) == 1 and responded and not any(KEY_FAILURE.search(k["status"]) for k in keys)
        and all(assoc["ts"] <= k["ts"] for k in keys) and responded[0]["ts"] <= verified[0]["ts"]) else FAIL
    checks["no_leave_or_rejoin"] = PASS if not m["rejoins"] and not m["leaves"] else FAIL
    if not assoc:
        return _verdict(checks, ev)
    nwk = assoc["nwk"]
    detail_nwk = int(detail["nwk"], 0) if isinstance(detail["nwk"], str) else int(detail["nwk"])
    if detail_nwk != nwk:
        checks["nwk_consistent"] = AMBIGUOUS
        return _verdict(checks, ev)
    try:
        app = application(window, nwk, ieee)
    except Ambiguous as error:
        checks["application_evidence"] = AMBIGUOUS
        checks["_ambiguity"] = [str(error)]
        return _verdict(checks, ev)
    ev["application"] = app
    after_keys = verified[0]["ts"] if verified else None
    checks["interview"] = PASS if (
        after_keys and app["initialized"] and all(i["ts"] >= after_keys and i["model"] == profile.model
                                                  and i["manufacturer"] == profile.manufacturer
                                                  for i in app["initialized"])
        and detail.get("model") == profile.model and detail.get("manufacturer") == profile.manufacturer) else FAIL
    sig_ok, ev["signature"] = signature_ok(detail, profile)
    checks["clusters"] = PASS if sig_ok else FAIL
    ok_bind = {b["cluster"] for b in app["bind"] if b["status"] == 0}
    checks["bind"] = PASS if set(profile.bind_clusters) <= ok_bind and all(
        b["status"] == 0 for b in app["bind"]) else FAIL
    checks["configure_reporting"] = PASS if all(
        app["configure"].get(c) and all(x["success"] for x in app["configure"][c])
        for c in profile.bind_clusters) else FAIL
    rep = lambda c: [x for x in app["reports"] if x["cluster"] == c]
    checks["temperature"] = PASS if -32768 < reads["temperature_raw"] < 32768 and rep(0x0402) else FAIL
    checks["humidity"] = PASS if 0 <= reads["humidity_raw"] <= 10000 and rep(0x0405) else FAIL
    checks["battery"] = PASS if (0 < reads["battery_voltage_raw"] < 255
                                 and 0 <= reads["battery_percentage_raw"] <= 200
                                 and app["configure"].get(0x0001)) else FAIL
    checks["identify_query"] = PASS if identify_ok(app, identify_active) else FAIL
    ev["nwk"] = nwk
    return _verdict(checks, ev)


def steady_state(records, ieee, nwk, soak_start, soak_end, reads, post_identify_active, profile=Profile()):
    """Continuous-operation view: no membership change, reports at the configured cadence."""
    ieee = ieee.lower()
    checks, ev = {}, {}
    try:
        covered(records, soak_start, soak_end)
        window = [r for r in records if soak_start <= r.ts <= soak_end]
        m = membership(window, ieee)
        app = application(window, nwk, ieee)
        everything = application([r for r in records if r.ts <= soak_end], nwk, ieee)
    except Ambiguous as error:
        return _verdict({"evidence": AMBIGUOUS, "_ambiguity": [str(error)]}, {})
    checks["no_association_rejoin_or_leave"] = PASS if not (
        m["associations"] or m["rejoins"] or m["leaves"] or m["child_joins"]) else FAIL
    checks["no_key_reestablishment"] = PASS if not m["keys"] else FAIL
    cadence, observations = {}, []
    for cluster, maximum in profile.report_max_interval:
        times = sorted(x["ts"] for x in everything["reports"] if x["cluster"] == cluster)
        configured = [x["ts"] for x in everything["configure"].get(cluster, ()) if x["success"]]
        prior = [t for t in configured[-1:] + times if t < soak_start]
        in_soak = [t for t in times if soak_start <= t]
        name = f"0x{cluster:04x}"
        if not prior:
            cadence[name] = dict(reports=len(in_soak), max_gap_s=None, ok=False)
            observations.append(f"{name}: no configure/report anchor before the soak")
            continue
        points = [max(prior)] + in_soak + [soak_end]
        gaps = [(b - a).total_seconds() for a, b in zip(points, points[1:])]
        observations += [f"{name}: gap {g:.0f}s > max interval" for g in gaps
                         if g > maximum + profile.report_tolerance_s]
        # One lost report (benign RF) is an observation; two consecutive are a failure.
        cadence[name] = dict(reports=len(in_soak), max_gap_s=max(gaps),
                             ok=len(in_soak) >= 2 and max(gaps) <= 2 * maximum + profile.report_tolerance_s)
    checks["reporting_continues"] = PASS if all(c["ok"] for c in cadence.values()) else FAIL
    sane = lambda r: (-32768 < r["temperature_raw"] < 32768 and 0 <= r["humidity_raw"] <= 10000
                      and 0 < r["battery_voltage_raw"] < 255 and 0 <= r["battery_percentage_raw"] <= 200)
    # Reads go through a polling end device: one failed round is a benign-RF observation;
    # two consecutive failed rounds or a failed final (post-soak) round is a failure.
    ok = [bool(r.get("ok")) for r in reads]
    checks["periodic_reads"] = PASS if (len(reads) >= 2 and ok[-1] and not any(
        a is False and b is False for a, b in zip(ok, ok[1:]))
        and all(sane(r["values"]) for r in reads if r.get("ok"))) else FAIL
    observations += [f"read round {i} failed" for i, good in enumerate(ok) if not good]
    checks["post_identify_query"] = PASS if identify_ok(app, post_identify_active) else FAIL
    ev.update(cadence=cadence, observations=observations, reports=len(app["reports"]),
              membership=dict(associations=len(m["associations"]), rejoins=len(m["rejoins"]),
                              leaves=len(m["leaves"]), keys=len(m["keys"])), reads=len(reads))
    return _verdict(checks, ev)


def firmware(value, symbol_rate=62500):
    """Terminal firmware snapshot (decoded by the operator). Latched/state facts judge
    commissioning; the security-keys last-operation result is a steady-state
    observation and fails only if the firmware itself treats it as fatal."""
    s, b, k = value["status"], value["bdb"], value["security_keys"]
    stack = value["stack"]
    limit = stack["static_limit_sp"]
    result = k["result"]
    clock_s = value["driver"]["now"] / symbol_rate
    checks = dict(
        gate_ready_latched=s["phase"] == JS_READY and s["reason"] == 0 and s["saw_ready"] == 1,
        bdb_ready=b["phase"] == BDB_READY and b["result"] == 0 and s["bdb_phase"] == BDB_READY
        and s["bdb_result"] == 0,
        membership=all(b[n] == 1 for n in ("member", "got_tc", "got_confirm")) and s["member"] == 1,
        security_keys_verified_state=k["phase"] == SECURITY_KEYS_VERIFIED,
        security_last_result_not_fatal=result < len(SECURITY_KEYS_RESULTS)
        and result not in SECURITY_KEYS_FATAL_RESULTS,
        driver_fault=value["driver"]["fault"] == 0,
        adapter_fault=value["adapter"]["fault"] == 0,
        radio_errors=not value["radio"]["errors"] & ~4 and not s["radio_errors"] & ~4,
        flash=value["flash"] == {"fault": 0, "result": 0},
        stack=(stack["within_admitted"] and limit == 0x7F + 106 and stack["sampled_sp"] <= limit
               and stack["nonzero_watermark_address"] is not None
               and stack["nonzero_watermark_address"] <= limit),
        bank_and_pc=(1 <= value["fmap"] <= 7 and bool(value["pc_location"])
                     and value["pc_location"]["within_size"] and not value["at_fault_thunk"]
                     and not value["halted_by_firmware"]),
        # A reset restarts the symbol clock; it must cover (almost) the whole resumed run.
        no_reset_clock=clock_s >= 0.95 * value["elapsed_seconds"] - 30
        and clock_s <= 1.05 * value["elapsed_seconds"] + 30)
    observation = dict(
        security_last_result=result,
        security_last_result_name=SECURITY_KEYS_RESULTS[result] if result < len(SECURITY_KEYS_RESULTS)
        else "UNKNOWN",
        peak_sp=stack["nonzero_watermark_address"], stack_bytes_lower_bound=stack["watermark_depth_lower_bound"],
        sampled_sp=stack["sampled_sp"], clock_seconds=round(clock_s, 1), elapsed_seconds=value["elapsed_seconds"],
        rx_frames=value["driver"]["rx_serial"], gaps=value["driver"]["gaps"],
        dropped=value["driver"]["dropped"], lossy=value["adapter"]["lossy"],
        radio_errors=value["radio"]["errors"], steps=s["steps"],
        pc=value["pc_location"])
    return _verdict({n: PASS if v else FAIL for n, v in checks.items()}, observation)
