# SPDX-License-Identifier: BSD-3-Clause
"""Offline SRAM admission and checked MEM-AP primitives, not a live operator.

The Tcl library has no top-level device operation. Its sole public operation
dispatcher permanently faults on an error. A future manual operator must bind
the selected target, preservation evidence and startup policy before using it.
This module neither connects to equipment nor grants permission to execute.
"""
import struct

from nrf_stimulus import artifact


ELF_SHA256 = "cfb862296d635953e8fe81b98ebf65582d79f68270e9680cb688c189ffd34e74"
HEX_SHA256 = "bda7868b2dbd0cdd7a1253557d90a842648b0fe093a4ef667e0bd616597866d3"
CONTROL_ADDRESS = 0x2000E048
CONTROL_SIZE = 2808
UART_READY_ADDRESS = 0x2000F020
RETAINED_COUNT_ADDRESS = 0x2000DDF8
CONTROL_OFFSETS = {
    "commands": 2734, "received": 2735, "state": 2736, "error": 2737,
    "input_nibbles": 2791, "consumed": 2792, "issued": 2793,
    "schedule_seen": 2794, "scheduled": 2795, "phy_done": 2796,
    "tx_retired": 2797, "stop_needed": 2798, "stop_issued": 2799,
    "stop_result_seen": 2800, "stop_accepted": 2801, "stopped": 2802,
    "fault_pending": 2803, "terminal_pending": 2804, "terminal_made": 2805,
    "sdk_ready": 2806, "startup_pending": 2807,
}
CONTROL_ABI_OFFSETS = dict(CONTROL_OFFSETS, arm_ms=2704, sequence=2724,
                           work=2726, input_work=2730, head=2732, count=2733,
                           nonce=2738, input=2746)


def payload(elf_bytes, hex_bytes):
    """Admit only the independently audited SRAM build, never a flash image."""
    artifact.require(type(elf_bytes) is bytes and type(hex_bytes) is bytes,
                     "Expected immutable ELF and HEX bytes")
    artifact.require(artifact.digest(elf_bytes) == ELF_SHA256 and
                     artifact.digest(hex_bytes) == HEX_SHA256,
                     "Not the accepted SRAM build")
    image = artifact.compare(elf_bytes, hex_bytes.decode("ascii"), ram_only=True)
    artifact.require(image.load_start == artifact.RAM_START and image.load_extent == 73220
                     and image.entry == 0x2000175D, "Accepted SRAM layout differs")
    data = bytes(image.memory[a] for a in range(image.load_start,
                                               image.load_start + image.load_extent))
    artifact.require(struct.unpack_from("<I", data)[0] == 0x20011580,
                     "Accepted SRAM stack differs")
    return data


def disarmed(control, uart_ready, retained_count):
    """Validate a coherent halted snapshot, not a running or RF observation."""
    artifact.require(type(control) is bytes and len(control) == CONTROL_SIZE,
                     "Expected the complete halted control object")
    artifact.require(type(uart_ready) is bytes and uart_ready == b"\x01" and
                     type(retained_count) is bytes and retained_count == bytes(4),
                     "UART not ready or retained receive buffers")
    artifact.require(control[2704:2724] == bytes(20) and
                     struct.unpack_from("<HHHH", control, 2724) == (1, 0, 0, 0) and
                     control[2732:2734] in (b"\x00\x01", b"\x01\x00"),
                     "Not the single HELLO startup state")
    for name, offset in CONTROL_OFFSETS.items():
        artifact.require(control[offset] == (1 if name in ("stopped", "sdk_ready") else 0),
                         f"Not healthy disarmed startup: {name}")
    artifact.require(control[2738:2791] == bytes(53), "Unexpected nonce or input")


TCL_LIBRARY = r"""
if {[info exists nrfh_state]} {error "handoff library already loaded"}
set nrfh_state new
set nrfh_vectors {}
set nrfh_payload {}
set nrfh_demcr 0

proc nrfh_u32 {value} {
    if {![regexp {^0x[0-9a-fA-F]{1,8}$} $value]} {error "invalid transfer word"}
    return [expr {$value + 0}]
}
proc nrfh_words {address count} {
    if {$::nrfh_state ne "busy"} {error "MEM-AP read outside owned operation"}
    if {$count < 1 || $count > 1024} {error "read count"}
    set raw [nrfp.mem read_memory $address 32 $count]
    if {$::nrfh_state ne "busy"} {error "handoff state changed during read"}
    if {[llength $raw] != $count} {error "short MEM-AP read"}
    set result {}
    foreach word $raw {lappend result [nrfh_u32 $word]}
    return $result
}
proc nrfh_read {address} {return [lindex [nrfh_words $address 1] 0]}
proc nrfh_expect {address mask expected} {
    if {([nrfh_read $address] & $mask) != $expected} {
        error "register predicate failed"
    }
}
proc nrfh_write {address value} {
    if {$::nrfh_state ne "busy"} {error "MEM-AP write outside owned operation"}
    if {$address ni {0xe000edf0 0xe000edf4 0xe000edf8 0xe000edfc
                     0xe000ed0c 0xe000ed08 0xe000ed30}} {
        error "write outside reviewed core registers"
    }
    nrfp.mem write_memory $address 32 [list [format 0x%08x $value]]
    if {$::nrfh_state ne "busy"} {error "handoff state changed during write"}
}
proc nrfh_status {} {
    set status [nrfh_read 0xe000edf0]
    if {$status & 0x02080020} {error "reset, lockup or imprecise debug entry"}
    return $status
}
proc nrfh_held {} {
    set status [nrfh_status]
    if {($status & 0x2002f) != 0x20003} {error "CPU not precisely held"}
    return $status
}
proc nrfh_ready {} {
    for {set i 0} {$i < 100} {incr i} {
        if {[nrfh_held] & 0x10000} {return}
        sleep 1
    }
    error "core transfer timeout"
}
proc nrfh_get {selector} {
    nrfh_held
    nrfh_write 0xe000edf4 $selector
    nrfh_ready
    return [nrfh_read 0xe000edf8]
}
proc nrfh_put {selector value} {
    nrfh_held
    nrfh_write 0xe000edf8 $value
    nrfh_write 0xe000edf4 [expr {0x10000 | $selector}]
    nrfh_ready
    if {[nrfh_get $selector] != $value} {error "core register readback differs"}
}
proc nrfh_idle {} {
    nrfh_expect 0x40010400 1 0
    nrfh_expect 0x4001e400 1 1
    nrfh_expect 0x4001e504 3 0
}
proc nrfh_halt {} {
    set status [nrfh_status]
    if {$status & 0x20000} {error "unexpected pre-existing halt"}
    if {($status & 1) && ($status & 0x0e)} {error "pre-existing debug control"}
    nrfh_idle
    nrfh_write 0xe000edf0 0xa05f0003
    for {set i 0} {$i < 100} {incr i} {
        set status [nrfh_status]
        if {($status & 0x2002f) == 0x20003} {
            nrfh_idle
            return
        }
        sleep 1
    }
    error "halt timeout"
}
proc nrfh_debug_quiet {} {
    global nrfh_demcr
    nrfh_expect 0xe0002000 1 0
    set nrfh_demcr [expr {[nrfh_read 0xe000edfc] & 0x010f07f1}]
    if {$nrfh_demcr & 0x000f07f1} {error "active debug monitor or vector catch"}
    nrfh_write 0xe000edfc 0x01000000
    nrfh_expect 0xe000edfc 0x010f07f1 0x01000000
    set count [expr {[nrfh_read 0xe0001000] >> 28}]
    for {set i 0} {$i < $count} {incr i} {
        nrfh_expect [expr {0xe0001028 + 16 * $i}] 15 0
    }
    nrfh_write 0xe000edfc $nrfh_demcr
    nrfh_expect 0xe000edfc 0x010f07f1 $nrfh_demcr
    nrfh_held
}
proc nrfh_quiescent {} {
    nrfh_held
    nrfh_idle
    nrfh_expect 0xe000e004 15 1
    foreach address {0xe000e100 0xe000e104 0xe000e200 0xe000e204
                     0xe000e300 0xe000e304 0xe000ed24} {
        nrfh_expect $address 0xffffffff 0
    }
    nrfh_expect 0xe000ed04 0x940001ff 0
    nrfh_expect 0xe000e010 7 0
    nrfh_expect 0xe000ed94 7 0
    nrfh_expect 0xe000ef34 1 0
    nrfh_expect 0xe000ed08 0xffffff80 0
    nrfh_expect 0x40001550 15 0
    nrfh_expect 0x4001f500 0xffffffff 0
    if {([nrfh_get 16] & 0x0700ffff) != 0x01000000 ||
        [nrfh_get 20] != 0} {error "reset did not establish privileged Thread mode"}
}
proc nrfh_reset {} {
    global nrfh_vectors nrfh_demcr
    nrfh_held
    nrfh_idle
    if {[nrfh_words 0x00000000 2] ne $nrfh_vectors} {error "original vectors changed"}
    nrfh_write 0xe000edfc [expr {$nrfh_demcr | 1}]
    nrfh_expect 0xe000edfc 0x010f07f1 [expr {$nrfh_demcr | 1}]
    nrfh_write 0xe000ed30 31
    nrfh_expect 0xe000ed30 31 0
    set aircr [expr {0x05fa0004 | ([nrfh_read 0xe000ed0c] & 0x700)}]
    nrfh_held
    nrfh_write 0xe000ed0c $aircr
    set seen 0
    set caught 0
    for {set i 0} {$i < 100} {incr i} {
        set status [nrfh_read 0xe000edf0]
        if {($status & 0x8002d) != 1} {error "invalid reset debug state"}
        if {$status & 0x02000000} {set seen 1}
        if {$seen && ($status & 0x20002) == 0x20000} {set caught 1; break}
        sleep 1
    }
    if {!$caught} {error "fresh reset catch not observed"}
    nrfh_expect 0xe000ed30 31 8
    nrfh_write 0xe000edf0 0xa05f0003
    nrfh_held
    nrfh_quiescent
    if {[nrfh_get 17] != [lindex $nrfh_vectors 0] ||
        [nrfh_get 15] != ([lindex $nrfh_vectors 1] & ~1) ||
        [nrfh_words 0x00000000 2] ne $nrfh_vectors} {
        error "not at the original reset vectors"
    }
    nrfh_held
}
proc nrfh_verify_ram {} {
    global nrfh_payload
    set count [llength $nrfh_payload]
    for {set i 0} {$i < $count} {incr i 64} {
        nrfh_held
        set end [expr {$i + 63 < $count ? $i + 63 : $count - 1}]
        if {[nrfh_words [expr {0x20000000 + 4 * $i}] [expr {$end - $i + 1}]] ne
            [lrange $nrfh_payload $i $end]} {error "SRAM readback differs"}
    }
    nrfh_held
}
proc nrfh_load {words} {
    global nrfh_payload
    set count [llength $words]
    if {$count < 16 || $count > 32768} {error "SRAM payload extent"}
    set parsed {}
    foreach word $words {lappend parsed [nrfh_u32 $word]}
    set sp [lindex $parsed 0]
    set pc [lindex $parsed 1]
    if {$sp <= 0x20000000 || $sp > 0x20020000 || ($sp & 7) ||
        !($pc & 1) || $pc < 0x20000008 || $pc >= 0x20000000 + 4 * $count} {
        error "SRAM vectors"}
    nrfh_quiescent
    set nrfh_payload $parsed
    for {set i 0} {$i < $count} {incr i 64} {
        nrfh_held
        set part [lrange $words $i [expr {$i + 63}]]
        set address [expr {0x20000000 + 4 * $i}]
        nrfp.mem write_memory $address 32 $part
        set readback [nrfh_words $address [llength $part]]
        if {$readback ne [lrange $parsed $i [expr {$i + 63}]]} {
            error "SRAM chunk readback differs"
        }
    }
    nrfh_verify_ram
}
proc nrfh_start {} {
    global nrfh_payload nrfh_demcr
    nrfh_quiescent
    nrfh_verify_ram
    nrfh_put 17 [lindex $nrfh_payload 0]
    nrfh_put 20 1
    nrfh_put 16 0x01000000
    nrfh_put 14 0xffffffff
    nrfh_put 15 [expr {[lindex $nrfh_payload 1] & ~1}]
    nrfh_write 0xe000ed08 0x20000000
    nrfh_expect 0xe000ed08 0xffffff80 0x20000000
    nrfh_write 0xe000edfc $nrfh_demcr
    nrfh_expect 0xe000edfc 0x010f07f1 $nrfh_demcr
    nrfh_held
    nrfh_write 0xe000edf0 0xa05f0001
    set released 0
    for {set i 0} {$i < 100} {incr i} {
        set status [nrfh_status]
        if {($status & 0x2f) != 1} {error "unexpected start control"}
        if {$status & 0x20000} {
            if {$released} {error "CPU halted again during start"}
        } else {
            set released 1
            if {$status & 0x01000000} {return}
        }
        sleep 1
    }
    error "instruction retirement not observed"
}
proc nrfh_operation {operation args} {
    global nrfh_state nrfh_vectors
    if {$nrfh_state in {fault busy}} {
        set nrfh_state fault
        error "handoff faulted or reentered"
    }
    set previous $nrfh_state
    set nrfh_state busy
    if {[catch {
        switch -- $operation {
            halt {
                if {$previous ne "new" || [llength $args] != 0} {error "halt order"}
                nrfh_expect 0xe000ed00 0xff0ffff0 0x410fc240
                # The first read alone discards reset history from before this operation.
                nrfh_read 0xe000edf0
                nrfh_halt
                nrfh_debug_quiet
                set nrfh_vectors [nrfh_words 0x00000000 2]
                set sp [lindex $nrfh_vectors 0]
                set pc [lindex $nrfh_vectors 1]
                if {$sp <= 0x20000000 || $sp > 0x20040000 || ($sp & 7) ||
                    !($pc & 1) || $pc < 9 || $pc >= 0x100000} {
                    error "invalid original vectors"
                }
                set next halted
            }
            reset {
                if {$previous ne "halted" || [llength $args] != 0} {error "reset order"}
                nrfh_reset
                set next caught
            }
            load {
                if {$previous ne "caught" || [llength $args] != 1} {error "load order"}
                nrfh_load [lindex $args 0]
                set next staged
            }
            start {
                if {$previous ne "staged" || [llength $args] != 0} {error "start order"}
                nrfh_start
                set next running
            }
            hold {
                if {$previous ne "running" || [llength $args] != 0} {error "hold order"}
                nrfh_halt
                set next observed-halt
            }
            return {
                if {$previous ne "observed-halt" || [llength $args] != 0} {
                    error "return order"
                }
                nrfh_debug_quiet
                nrfh_reset
                set next original-reset-held
            }
            default {error "unsupported handoff operation"}
        }
        if {$nrfh_state ne "busy"} {error "handoff lost operation ownership"}
    } result]} {
        set nrfh_state fault
        return -code error $result
    }
    set nrfh_state $next
    return $nrfh_state
}
"""
