#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Offline-only exact real-MAC smoke image admission and sparse physical pack."""
import argparse
import json
from pathlib import Path
import re

import boot_banked as banking
from banked_image import pack, ihex
from boot_mac_link_child_abi import allocations, ascii_text, full_locations, object_body as code_object
from verify_banked_join import transfers, live_data
from verify_mac_adapter import LOWER, RUNTIME
from verify_firmware import parse_ihex, parse_symbols, require, cdb_address
from boot_mac_attempt import mmio_sites


def modules(board):
    require(board in ("generic", "lg_esl29_rev03"), "Smoke board")
    return ("mac_smoke_iram_low", "banked", "mac_smoke_iram_high")+LOWER+(
        "mac_frame", "mac_tx", "mac_adapter", "startup", "status", board,
        "mac_smoke", "mac_smoke_main")


def object_body(raw, module):
    if module in ("mac_smoke_iram_low", "mac_smoke_iram_high", "generic"):
        text = ascii_text(raw, "declaration-only object")
        require(text.startswith("XH3\n") and re.findall(r"^M (\w+)$",text,re.M)==[module],
                "Smoke declaration-only object")
        return raw
    return code_object(raw,module)


def load(output, board):
    path = output/"mac_smoke.ihx"
    return (parse_ihex(path.read_text("ascii")), path.with_suffix(".map").read_bytes(),
            path.with_suffix(".cdb").read_bytes(), path.with_suffix(".mem").read_bytes(),
            {m: (output/f"mac_smoke.{m}.rst").read_bytes() for m in modules(board)},
            {m: (output/f"{m}.rel").read_bytes() for m in modules(board)})


def map_parts(raw, names):
    ascii_text(raw, "map")
    require(raw.count(b"Files Linked") == raw.count(b"Libraries Linked") == 1, "Smoke map sections")
    prefix, rest = raw.split(b"Files Linked")
    files, suffix = rest.split(b"Libraries Linked")
    header = b"                              [ module(s) ]\n\n"
    require(files.startswith(header) and files.endswith(b"\n\n\n"), "Smoke file-table framing")
    lines = files[len(header):-3].decode("ascii").split("\n")
    # ASlink places <=40-character paths on one padded row; longer paths have
    # a separate exact module row. This is only BUILD/output path formatting.
    paths=[]
    while lines:
        line=lines.pop(0)
        if line.endswith("[  ]"):
            path=line[:42].rstrip(" ")
            require(len(path)<=40 and line==path.ljust(42)+"[  ]","Smoke short file row")
        else:
            path=line
            require(len(path)>40 and lines and lines.pop(0)==" "*42+"[  ]","Smoke long file row")
        require(path and not any(c.isspace() for c in path),"Smoke file path syntax")
        paths.append(Path(path))
    require(tuple(p.name for p in paths) == tuple(m+".rel" for m in names) and
            len({p.parent for p in paths}) == 1, "Smoke link order/objects")
    return prefix, suffix


def identities(artifacts, board):
    image, mapping, debug, memory, listings, objects = artifacts
    names = modules(board)
    require(type(debug) is bytes and set(listings) == set(objects) == set(names),
            "Smoke raw CDB and complete inventories")
    prefix, suffix = map_parts(mapping, names)
    return {
        "code": banking.sha(b"".join(a.to_bytes(4, "big")+bytes([image[a]]) for a in sorted(image))),
        "cdb": banking.sha(debug), "memory": banking.sha(memory),
        "map_prefix": banking.sha(prefix), "map_suffix": banking.sha(suffix),
        "objects": banking.sha(b"".join(m.encode()+b"\0"+object_body(objects[m],m) for m in names)),
        "listings": banking.sha(b"".join(m.encode()+b"\0"+listings[m] for m in names)),
    }


def symbols_for(mapping, debug):
    # Every byte of the raw MAP is bound above. ASlink's truncated rows are NOT
    # full identities: supplement actual globals from full immutable raw CDB.
    symbols = parse_symbols(mapping.decode("ascii"))
    for name, address in full_locations(debug).items():
        if name.startswith("G$"):
            key = "_"+name.split("$")[1]
            require(key not in symbols or symbols[key] == address, "Smoke conflicting full identity")
            symbols[key] = address
    return symbols


def structure(artifacts, board):
    image, mapping, raw, memory, listings, objects = artifacts
    names = modules(board)
    map_parts(mapping, names)
    debug = ascii_text(raw, "CDB")
    symbols = symbols_for(mapping, debug)
    require(re.findall(r"^M:(\w+)$", debug, re.M) == list(names), "Smoke source/probe separation")
    require(not any(n.startswith(("_fixture_", "_host_", "_test_", "_traced_")) for n in symbols),
            "Simulator caller/model entered board CODE")
    areas = ("HOME", "GSINIT0", "GSINIT1", "GSINIT2", "GSINIT3", "GSINIT4", "GSINIT5",
             "GSINIT", "GSFINAL", "CSEG", "CONST", "MA_BANK1", "MA_BANK2")
    coverage = set()
    for area in areas:
        span = set(range(symbols["s_"+area], symbols["s_"+area]+symbols["l_"+area]))
        require(not coverage & span, "Smoke CODE overlap")
        coverage |= span
    require(coverage == set(image) and symbols["s_CONST"]+symbols["l_CONST"] <= 0x8000,
            "Smoke CODE coverage/common CONST")
    physical = pack(image)
    require(len(physical) == len(image) and max(physical) < 0x3e800, "Smoke physical reserved flash")
    require((symbols["s_SSEG"], symbols["l_SSEG"], symbols["s_OSEG"], symbols["l_OSEG"],
             symbols["s_BSEG_BYTES"], symbols["l_BSEG_BYTES"], symbols["_banked_depth"],
             symbols["_banked_fault"], symbols["_m0_status"], symbols["s_XSEG"]) ==
            (0x56,0x27,0x4c,10,0x20,3,0x1e,0x1f,0x1e00,0), "Smoke physical IRAM/XDATA/M0 ABI")
    require(symbols["l_XSEG"] <= 0x1e00 and symbols["__XPAGE"] == 0x93, "Smoke nonaliased XDATA/MPAGE")
    require(b"16 bit mode initial stack starts at: 0x56 (sp set to 0x55) with 39 bytes available." in memory,
            "Smoke actual stack reservation")
    require(all(symbols["l_"+a] == 0 for a in ("XABS","XISEG","XINIT","PSEG","ISEG","IABS","BIT_BANK")),
            "Smoke implicit storage")
    require((symbols["_mac_smoke_iram_low"],symbols["_mac_smoke_iram_high"]) == (8,0x23),
            "Smoke physical DATA backing")
    retained = re.findall(r"^S:([FG][^(]+)\(\{(\d+)\}.*\),E,0,0$",debug,re.M)
    require({(k.split("$")[1],int(n)) for k,n in retained} ==
            {("mac_smoke_iram_low",22),("mac_smoke_iram_high",41),("banked_depth",1),("banked_fault",1)},
            "Smoke retained DATA ownership")
    spans, offset = {}, 0
    for m in names:
        text = ascii_text(listings[m], "listing")
        require(f".module {m}" in text, "Smoke swapped listing")
        object_body(objects[m], m)
        owned = set()
        for a, n in allocations(text).get("XSEG", []):
            span = set(range(a,a+n))
            require(span and not span & owned, "Smoke XDATA overlap")
            owned |= span
        require(owned == set(range(offset,offset+len(owned))), "Smoke unowned/noncontiguous XDATA")
        offset += len(owned); spans[m] = owned
    runtime = {"___memcpy_PARM_2":0, "___memcpy_PARM_3":3, "_memset_PARM_2":8,
               "_memset_PARM_3":9, "__gptrput_PARM_2":11, "__mullong_PARM_2":12}
    require(symbols["l_XSEG"] == offset+16 and all(symbols[k] == offset+v for k,v in runtime.items()),
            "Smoke actual libc fence")
    shared, radio, attempt, adapter = (symbols[n] for n in (
        "_mac_radio_shared_end","_mac_radio_reserved_end","_mac_attempt_reserved_end","_mac_adapter_reserved_end"))
    require(set().union(*(spans[m] for m in names[:8])) == set(range(shared)) and
            spans["mac_radio"] == set(range(shared,radio+1)) and
            spans["mac_attempt"] == set(range(radio+1,attempt+1)) and
            set().union(*(spans[m] for m in names[10:13])) == set(range(attempt+1,adapter+1)) and
            set().union(*(spans[m] for m in names[13:])) == set(range(adapter+1,offset)),
            "Smoke complete private-prefix/caller separation")
    graph = transfers(image,symbols,listings,debug,modules=names,
                      areas=("CSEG","MA_BANK1","MA_BANK2"),library=RUNTIME,indirect_sites=())
    frame_areas = {m:"MA_"+m for m in names}
    frame_areas.update(startup="MS_START",status="MS_STATUS",mac_smoke="MS_CALLER",
                       mac_smoke_main="MS_MAIN",**{board:"MS_BOARD"})
    liveness = live_data(symbols,debug,listings,*graph,modules=names,
                         reservations=set(range(8,0x1e))|set(range(0x23,0x4c)),
                         overlay=set(range(0x4c,0x56)),frame_areas=frame_areas,
                         physical=("mac_smoke_iram_low","mac_smoke_iram_high"))
    sites = mmio_sites(image,debug,{m:v.decode("ascii") for m,v in listings.items()},handoff=True)
    # 171: radio_autoack complete_head() merged the two adapter PHR (0x619A) reads.
    require(len(sites)==171, "Smoke unchanged lower MMIO inventory")
    for name in ("wait","end","fault"):
        require(symbols["_mac_smoke_"+name] < 0x8000, "Smoke checkpoint outside common CODE")
    require("S:G$mac_smoke_status$0_0$0({64}" in debug and
            "S:G$mac_smoke_mailbox$0_0$0({8}" in debug and
            "S:G$m0_status$0_0$0({32}" in debug, "Smoke byte-only status/mailbox/M0 sizes")
    return symbols, liveness


def verify(artifacts, board):
    expected = json.loads(Path(__file__).with_name("mac_smoke_pins.json").read_text("ascii"))[board]
    # No decoding of CDB precedes its immutable complete-byte identity.
    # Pins refreshed for radio_autoack complete_head()/RXOVERF resample relink.
    require(type(artifacts[2]) is bytes and banking.sha(artifacts[2]) == expected["cdb"], "Smoke complete raw CDB")
    require(identities(artifacts,board) == expected, "Smoke complete immutable artifacts")
    return structure(artifacts,board)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--board",choices=("generic","lg_esl29_rev03"),required=True)
    parser.add_argument("--pack",type=Path)
    args=parser.parse_args()
    artifacts=load(args.output,args.board)
    symbols,liveness=verify(artifacts,args.board)
    physical=pack(artifacts[0])
    if args.pack:
        text=ihex(physical)
        require(parse_ihex(text)==physical,"Smoke independent physical HEX round trip")
        args.pack.write_text(text,encoding="ascii")
    print(json.dumps({"board":args.board,"code":len(physical),"xdata":symbols["l_XSEG"],
                      "physical_last":max(physical),"liveness":liveness,
                      "addresses":{k:symbols["_mac_smoke_"+k] for k in ("status","mailbox","wait","end","fault")}}))


if __name__=="__main__":
    main()
