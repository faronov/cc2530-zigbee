# SPDX-License-Identifier: BSD-3-Clause
"""Checked Tcl transactions against original synthetic MMIO; no equipment."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import nrf_handoff as handoff


def abi_source():
    assertions = [
        '#include "control.h"',
        f'_Static_assert(sizeof(struct stim_control) == {handoff.CONTROL_SIZE}, "size");',
    ]
    for name, offset in handoff.CONTROL_ABI_OFFSETS.items():
        assertions.append(f'_Static_assert(offsetof(struct stim_control, {name}) == '
                          f'{offset}, "{name}");')
    return "\n".join(assertions) + "\n"


BACKEND = r"""
set scenario [lindex $argv 0]
set calls 0
set sleeps 0
set controls 0
set halted 0
set reset_sticky 1
set retired 0
set pending {}
set pending_ticks 0
set data 0
set reset_count 0
set release_ticks 0
set reset_ticks 0
set phase initial
set trace {}
array set mem {}
array set core {15 0x1234 16 0x01000003 17 0x20030000 20 0x02008001}
foreach {a v} {
    0x00000000 0x20040000 0x00000004 0x00000101
    0xe000ed00 0x410fc241 0xe000edfc 0 0xe000ed30 0
    0xe000ed0c 0xfa050300 0xe0002000 0 0xe0001000 0x40000000
    0xe0001028 0 0xe0001038 0 0xe0001048 0 0xe0001058 0
    0x40010400 0 0x4001e400 1 0x4001e504 0
    0xe000e004 1 0xe000ed04 0 0xe000e010 0 0xe000ed94 0
    0xe000ef34 0 0xe000ed08 0 0x40001550 0 0x4001f500 0
    0xe000e100 0 0xe000e104 0 0xe000e200 0 0xe000e204 0
    0xe000e300 0 0xe000e304 0 0xe000ed24 0
} {set mem([expr {$a + 0}]) [expr {$v + 0}]}

proc sleep {milliseconds} {
    global sleeps
    if {$milliseconds != 1} {error "unbounded sleep"}
    incr sleeps
}
proc action {kind address count} {
    global calls trace scenario
    incr calls
    lappend trace [list $kind $address $count]
    if {$scenario eq "fail-$calls"} {error "injected transfer failure"}
    if {$scenario eq "reentrant" && $calls == 10} {
        catch {nrfh_operation halt}
    }
}
proc complete_transfer {} {
    global pending core data scenario
    lassign $pending selector writing value
    if {$writing} {
        if {$selector == 16 && (($core(16) ^ $value) & 0x1ff)} {
            error "debugger attempted to change IPSR"
        }
        if {$selector == 15 && ($value & 1)} {error "odd DebugReturnAddress"}
        set core($selector) $value
    } else {
        set data $core($selector)
        if {$scenario eq "register-$selector" && $selector in {14 15 16 17 20} &&
            [info exists core(written-$selector)]} {set data [expr {$data ^ 8}]}
    }
    if {$writing} {set core(written-$selector) 1}
    set pending {}
}
proc read_word {address} {
    global mem controls halted reset_sticky retired pending pending_ticks data scenario phase
    global release_ticks reset_ticks
    if {$address == 0xe000edf0} {
        if {$reset_ticks > 0} {incr reset_ticks -1; return 0x00030003}
        if {$release_ticks > 0} {
            incr release_ticks -1
            if {$release_ticks == 0} {set halted 0; set retired 1}
        }
        if {$pending ne {} && $scenario ne "transfer-timeout"} {
            incr pending_ticks -1
            if {$pending_ticks == 0} {complete_transfer}
        }
        set result [expr {$controls | ($halted << 17) | (($pending eq {}) << 16) |
                          ($reset_sticky << 25) | ($retired << 24)}]
        if {$scenario eq "lockup" && $phase eq "running"} {set result [expr {$result | 0x80000}]}
        set reset_sticky 0
        set retired 0
        return $result
    }
    if {$address == 0xe000edf8} {
        if {$pending ne {}} {error "DCRDR read before S_REGRDY"}
        return $data
    }
    if {$address >= 0xe0001000 && $address <= 0xe0001058 &&
        !($mem(3758157308) & 0x1000000)} {error "DWT read with TRCENA off"}
    if {![info exists mem($address)]} {error "unexpected read address"}
    return $mem($address)
}
proc write_word {address value} {
    global mem controls halted reset_sticky retired pending pending_ticks data
    global core scenario phase reset_count
    global release_ticks reset_ticks
    switch -- [format %08x $address] {
        e000edf0 {
            if {($value >> 16) != 0xa05f || ($value & 0xfffc) != 0} {
                error "unexpected DHCSR control write"
            }
            if {($controls & 1) && !$halted && (($controls ^ $value) & 12)} {
                error "illegal running debug-control change"
            }
            set controls [expr {$value & 3}]
            if {$controls & 2} {
                if {$scenario ne "halt-timeout"} {set halted 1}
            } else {
                if {!$halted || $phase ne "reset"} {error "unexpected start"}
                if {$core(16) != 0x01000000 || $core(20) != 1 ||
                    $core(17) != 0x20000100 || $core(15) != 0x20000008 ||
                    $mem(3758157064) != 0x20000000} {error "wrong SRAM entry context"}
                set halted 0
                set retired [expr {$scenario ne "no-retirement"}]
                if {$scenario eq "release-delayed"} {
                    set release_ticks 3
                    set halted 1
                    set retired 0
                }
                if {$scenario eq "unexpected-reset"} {set reset_sticky 1}
                set phase running
            }
        }
        e000edf4 {
            if {!$halted || $pending ne {}} {error "DCRSR while running or busy"}
            set selector [expr {$value & 127}]
            if {$selector ni {14 15 16 17 20}} {error "unsupported selector"}
            set pending [list $selector [expr {($value >> 16) & 1}] $data]
            set pending_ticks 2
        }
        e000edf8 {
            if {!$halted || $pending ne {}} {error "DCRDR write while busy"}
            set data $value
        }
        e000ed30 {set mem($address) [expr {$mem($address) & ~$value}]}
        e000edfc - e000ed08 {
            if {!$halted} {error "core setup while running"}
            set mem($address) $value
        }
        e000ed0c {
            if {$value != 0x05fa0304 || !($mem(3758157308) & 1) || !$halted} {
                error "not a reviewed SYSRESETREQ with catch"
            }
            incr reset_count
            set phase reset
            set controls 1
            set reset_sticky [expr {$scenario ne "missing-reset"}]
            if {$scenario eq "reset-delayed"} {set reset_ticks 3}
            set mem(3758157104) [expr {$scenario eq "missing-catch" ? 0 : 8}]
            set mem(3758157064) 0
            set core(16) [expr {$scenario eq "active-exception" ? 0x01000003 : 0x01000000}]
            set core(17) $mem(0)
            set core(15) [expr {$mem(4) & ~1}]
            set core(20) 0
            if {$scenario eq "wrong-reset-pc"} {set core(15) 0x104}
        }
        default {
            if {!$halted || $phase ne "reset" ||
                $address < 0x20000000 || $address >= 0x20020000} {
                error "write outside SRAM"
            }
            set mem($address) $value
        }
    }
}
proc nrfp.mem {command address width values} {
    global calls scenario
    set address [expr {$address + 0}]
    if {$width != 32 || $address % 4} {error "memory access width/alignment"}
    if {$command eq "read_memory"} {
        action R $address $values
        set result {}
        for {set i 0} {$i < $values} {incr i} {
            lappend result [format 0x%08x [read_word [expr {$address + 4 * $i}]]]
        }
        if {$scenario eq "short-$calls"} {return [lrange $result 1 end]}
        if {$scenario eq "malformed-$calls"} {lset result 0 {[exec forbidden]}}
        if {$scenario eq "chunk-mismatch" && $address == 0x20000100} {
            lset result 0 0x00000000
        }
        return $result
    }
    if {$command ne "write_memory"} {error "synthetic CPU helpers forbidden"}
    action W $address [llength $values]
    foreach value $values {
        if {![regexp {^0x[0-9a-fA-F]{1,8}$} $value]} {error "write word syntax"}
        write_word $address [expr {$value + 0}]
        incr address 4
    }
}
"""

DRIVER = r"""
set payload {0x20000100 0x20000009}
for {set i 2} {$i < 130} {incr i} {lappend payload [format 0x%08x $i]}
if {$scenario eq "extent-min"} {set payload [lrange $payload 0 15]}
if {$scenario eq "extent-max"} {
    set payload [concat [lrange $payload 0 1] [lrepeat 32766 0x00000000]]
}
if {[regexp {^predicate-(0x[0-9a-f]+)-(0x[0-9a-f]+)$} $scenario all address value]} {
    set mem([expr {$address + 0}]) [expr {$value + 0}]
}
if {$scenario eq "pre-halted"} {set halted 1}
if {$scenario eq "pre-debug-step"} {set controls 5}
if {[regexp {^bad-payload-(.*)$} $scenario all kind]} {
    switch -- $kind {
        small {set payload [lrange $payload 0 14]}
        large {set payload [concat [lrange $payload 0 1] [lrepeat 32767 0x00000000]]}
        syntax {lset payload 129 {[exec forbidden]}}
        stack {lset payload 0 0x20020008}
        stack-align {lset payload 0 0x20000104}
        pc-flash {lset payload 1 0x00000009}
        pc-even {lset payload 1 0x20000008}
        pc-end {lset payload 1 0x20000209}
    }
}
set stages {}
set code [catch {
    if {[regexp {^order-(.*)$} $scenario all op]} {nrfh_operation $op}
    lappend stages [nrfh_operation halt]
    lappend stages [nrfh_operation reset]
    lappend stages [nrfh_operation load $payload]
    if {$scenario eq "late-mismatch"} {set mem(536870916) 0x2000000b}
    lappend stages [nrfh_operation start]
    lappend stages [nrfh_operation hold]
    lappend stages [nrfh_operation return]
} result]
puts "RESULT $code $nrfh_state $calls $sleeps"
puts "STAGES $stages"
puts "RESETS $reset_count"
puts "DETAIL $result"
foreach item $trace {puts "TRACE $item"}
if {$code} {
    if {$nrfh_state ne "fault"} {error "failure not latched"}
    set before $calls
    foreach operation {halt reset load start hold return} {
        if {![catch {nrfh_operation $operation}]} {error "faulted call succeeded"}
    }
    if {$calls != $before} {error "I/O after failure"}
}
"""


class HandoffTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tcl = shutil.which(os.environ.get("NRF_HANDOFF_TEST_INTERPRETER", "tclsh8.6"))
        if cls.tcl is None:
            raise AssertionError("The selected standalone Tcl interpreter is required")
        cls.temp = tempfile.TemporaryDirectory(prefix="nrf-handoff-offline-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        cls.script = cls.root / "synthetic.tcl"
        cls.script.write_text(BACKEND + handoff.TCL_LIBRARY + DRIVER, encoding="ascii")

    def run_case(self, scenario="good"):
        result = subprocess.run([self.tcl, str(self.script), scenario],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertEqual(result.stderr, "")
        lines = result.stdout.splitlines()
        row = lines[0].split()
        traces = [tuple(map(int, line.split()[2:])) + (line.split()[1],)
                  for line in lines if line.startswith("TRACE ")]
        return row[1:], traces, result.stdout

    def failure(self, scenario, *, before_start=True):
        row, trace, output = self.run_case(scenario)
        self.assertEqual(row[:2], ["1", "fault"], output)
        if before_start:
            self.assertNotIn("running", output.split("STAGES ", 1)[1].split("\n", 1)[0])
        return row, trace, output

    def test_complete_real_tcl_flow_stops_at_original_reset_not_resume(self):
        row, trace, output = self.run_case()
        self.assertEqual(row[:2], ["0", "original-reset-held"], output)
        self.assertIn("STAGES halted caught staged running observed-halt original-reset-held", output)
        self.assertIn("RESETS 2", output)
        self.assertGreater(int(row[3]), 10, "must actually wait for core transfers")
        writes = [address for address, _, kind in trace if kind == "W"]
        self.assertEqual(writes.count(0xe000ed0c), 2)
        self.assertTrue(all(address in (0xe000edf0, 0xe000edf4, 0xe000edf8, 0xe000edfc,
                                       0xe000ed0c, 0xe000ed08, 0xe000ed30) or
                            0x20000000 <= address < 0x20020000 for address in writes))

    def test_every_transfer_error_stops_without_retry_or_cleanup_io(self):
        row, trace, output = self.run_case()
        self.assertEqual(row[:2], ["0", "original-reset-held"], output)
        for index in range(1, len(trace) + 1):
            with self.subTest(transfer=index):
                failed, actual, _ = self.failure(f"fail-{index}", before_start=False)
                self.assertEqual(int(failed[2]), index)
                self.assertEqual(actual, trace[:index])

    def test_every_read_rejects_short_and_malformed_results(self):
        _, trace, _ = self.run_case()
        for index, (_, _, kind) in enumerate(trace, 1):
            if kind != "R":
                continue
            for damage in ("short", "malformed"):
                with self.subTest(transfer=index, damage=damage):
                    row, actual, _ = self.failure(f"{damage}-{index}", before_start=False)
                    self.assertEqual(int(row[2]), index)
                    self.assertEqual(actual, trace[:index])

    def test_timeouts_catch_loss_and_unsafe_inherited_state(self):
        for scenario in ("halt-timeout", "transfer-timeout", "missing-reset", "missing-catch",
                         "active-exception", "wrong-reset-pc", "pre-halted", "pre-debug-step"):
            with self.subTest(scenario=scenario):
                row, _, _ = self.failure(scenario)
                self.assertLessEqual(int(row[3]), 100)
        for scenario in ("no-retirement", "lockup", "unexpected-reset"):
            with self.subTest(scenario=scenario):
                self.failure(scenario)

    def test_core_write_readback_and_both_sram_readbacks_are_required(self):
        for selector in (14, 15, 16, 17, 20):
            with self.subTest(selector=selector):
                self.failure(f"register-{selector}")
        for scenario in ("chunk-mismatch", "late-mismatch"):
            with self.subTest(scenario=scenario):
                self.failure(scenario)

    def test_asynchronous_reset_and_release_are_actually_polled(self):
        for scenario in ("reset-delayed", "release-delayed"):
            with self.subTest(scenario=scenario):
                row, _, output = self.run_case(scenario)
                self.assertEqual(row[:2], ["0", "original-reset-held"], output)

    def test_reset_predicates_and_initial_debug_features(self):
        for address, value in (
            (0xe000ed00, 0x410fc231), (0x40010400, 1), (0x4001e400, 0),
            (0x4001e504, 1), (0x4001e504, 2), (0xe0002000, 1),
            (0xe000edfc, 0x10000), (0xe000edfc, 1),
            (0xe0001028, 1), (0xe0001058, 15),
            (0xe000e004, 0), (0xe000e100, 1), (0xe000e104, 1),
            (0xe000e200, 1), (0xe000e204, 1), (0xe000e300, 1),
            (0xe000e304, 1), (0xe000ed24, 1), (0xe000ed04, 0x80000000),
            (0xe000e010, 1), (0xe000ed94, 1), (0xe000ef34, 1),
            (0x40001550, 1), (0x4001f500, 1),
            (0, 0x20040008), (0, 0x20000000), (4, 0x100001),
            (4, 0x100), (4, 1),
        ):
            with self.subTest(address=hex(address), value=hex(value)):
                self.failure(f"predicate-0x{address:x}-0x{value:x}")

    def test_invalid_payload_is_rejected_before_any_sram_write(self):
        for kind in ("small", "large", "syntax", "stack", "stack-align",
                     "pc-flash", "pc-even", "pc-end"):
            with self.subTest(kind=kind):
                _, trace, _ = self.failure(f"bad-payload-{kind}")
                self.assertFalse(any(k == "W" and 0x20000000 <= a < 0x20040000
                                     for a, _, k in trace))

    def test_exact_sram_transfer_boundaries(self):
        for scenario, size in (("extent-min", 64), ("extent-max", 131072)):
            with self.subTest(scenario=scenario):
                row, trace, output = self.run_case(scenario)
                self.assertEqual(row[:2], ["0", "original-reset-held"], output)
                writes = [(a, n) for a, n, k in trace if k == "W" and
                          0x20000000 <= a < 0x20040000]
                self.assertEqual(sum(n * 4 for _, n in writes), size)
                self.assertEqual(writes[-1][0] + writes[-1][1] * 4, 0x20000000 + size)

    def test_reordered_or_unknown_operations_fault_before_io(self):
        for operation in ("reset", "load", "start", "hold", "return", "resume", "recover"):
            with self.subTest(operation=operation):
                row, trace, _ = self.failure(f"order-{operation}")
                self.assertEqual(row[2], "0")
                self.assertEqual(trace, [])

    def test_reentrant_attempt_stays_faulted_even_if_the_backend_catches_it(self):
        row, trace, _ = self.failure("reentrant")
        self.assertEqual(int(row[2]), 10)
        self.assertEqual(len(trace), 10)

    def test_library_loading_has_no_io_and_cannot_rearm_a_session(self):
        script = self.root / "load-only.tcl"
        script.write_text(handoff.TCL_LIBRARY + '\nset nrfh_state fault\n'
                          'if {![catch {\n' + handoff.TCL_LIBRARY +
                          '\n}]} {error "reloaded"}\n'
                          'if {$nrfh_state ne "fault"} {error "rearmed"}\n', encoding="ascii")
        result = subprocess.run([self.tcl, str(script)], capture_output=True, text=True, timeout=5)
        self.assertEqual((result.returncode, result.stdout, result.stderr), (0, "", ""))

    def test_native_control_abi_is_compile_checked(self):
        self.assertIsNotNone(shutil.which("cc"), "C compiler required for actual control ABI")
        result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                                 "-I", str(Path(__file__).parent / "nrf_stimulus"),
                                 "-x", "c", "-c", "-o", str(self.root / "abi.o"), "-"],
                                input=abi_source(), capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_actual_portable_startup_and_hello_drain_match_observation_contract(self):
        source = abi_source() + r"""
#include <stdio.h>
static int emit(const struct stim_control *s)
{
    return fwrite(s, 1, sizeof(*s), stdout) == sizeof(*s) ? 0 : 1;
}
int main(void)
{
    struct stim_control s;
    struct stim_record record;
    if (!stim_startup_begin(&s, 5, true)) return 1;
    stim_startup_complete(&s, 10, true, true);
    if (emit(&s) || !stim_pop(&s, &record)) return 2;
    stim_tick(&s, 15);
    if (emit(&s) || !stim_startup_begin(&s, 20, true)) return 3;
    stim_startup_complete(&s, 25, false, true);
    return emit(&s);
}
"""
        program = self.root / "observe-control"
        directory = Path(__file__).parent / "nrf_stimulus"
        result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                                 "-I", str(directory), "-x", "c", "-", str(directory / "control.c"),
                                 "-o", str(program)],
                                input=source, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([str(program)], capture_output=True, timeout=5)
        self.assertEqual((result.returncode, result.stderr), (0, b""))
        self.assertEqual(len(result.stdout), 3 * handoff.CONTROL_SIZE)
        for index in (0, 1):
            handoff.disarmed(result.stdout[index * handoff.CONTROL_SIZE:
                                           (index + 1) * handoff.CONTROL_SIZE],
                             b"\x01", bytes(4))
        with self.assertRaises(ValueError):
            handoff.disarmed(result.stdout[2 * handoff.CONTROL_SIZE:], b"\x01", bytes(4))

    def test_disarmed_requires_the_entire_healthy_state_not_sdk_ready_alone(self):
        data = bytearray(handoff.CONTROL_SIZE)
        struct.pack_into("<H", data, 2724, 1)
        data[2733] = 1
        for name in ("sdk_ready", "stopped"):
            data[handoff.CONTROL_OFFSETS[name]] = 1
        handoff.disarmed(bytes(data), b"\x01", bytes(4))
        for name, offset in handoff.CONTROL_OFFSETS.items():
            with self.subTest(field=name):
                bad = data.copy()
                bad[offset] ^= 1
                with self.assertRaises(ValueError):
                    handoff.disarmed(bytes(bad), b"\x01", bytes(4))
        for offset in range(2738, 2791):
            bad = data.copy()
            bad[offset] = 1
            with self.assertRaises(ValueError):
                handoff.disarmed(bytes(bad), b"\x01", bytes(4))
        for offset in range(2704, 2734):
            bad = data.copy()
            bad[offset] ^= 1
            with self.assertRaises(ValueError):
                handoff.disarmed(bytes(bad), b"\x01", bytes(4))
        drained = data.copy()
        drained[2732:2734] = b"\x01\x00"
        handoff.disarmed(bytes(drained), b"\x01", bytes(4))
        for control, uart, retained in ((bytes(data[:-1]), b"\x01", bytes(4)),
                                        (bytes(data), b"\x00", bytes(4)),
                                        (bytes(data), b"\x01", b"\x01\x00\x00\x00"),
                                        (data, b"\x01", bytes(4))):
            with self.assertRaises(ValueError):
                handoff.disarmed(control, uart, retained)

    def test_unaccepted_artifacts_never_reach_the_image_parser(self):
        with patch.object(handoff.artifact, "compare", side_effect=AssertionError("not admitted")):
            for elf, ihex in ((b"ELF", b"HEX"), (bytearray(32), b"HEX"), (b"ELF", "HEX")):
                with self.assertRaises(ValueError):
                    handoff.payload(elf, ihex)


if __name__ == "__main__":
    unittest.main()
