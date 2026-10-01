#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Exact co-owned live RX/AUTOACK handoff; synthetic hardware only."""
from boot_mac_attempt import CALLER_SIZES, MODULES, Profile, main


HANDOFF = Profile(
    stem="mac_handoff_test", prefix="mh", native="host-mac-handoff-tests",
    modules=MODULES[:-1]+("test_mac_handoff",),
    caller_sizes=tuple(CALLER_SIZES.items())+(("clock", 6),),
    size=27973, xdata=1537, stack=0x5c,
    code_sha="9c391b9b21091663e809910eadfd6203b74be5c5e1364521793e74b750c5880d",
    cdb_sha="4225b3c1ada3a30eaa25111ad998bf8cdb9c3bb11b74324f8fbf96b664045d19",
    map_sha="8a2d5e78b01c8f71033b64c2ad4a9cbc534ebd21361bc5a00cd06c88efc0ad23",
    mem_sha="3997d5dabe368f189112a30c9a65bccfc0820228722f4c83cbfb34efca797425",
    list_sha="f6b123726aca309c62414f77a3a10937492f6a46709a64a1dc7650e2d19cec32",
    object_sha="52531d611847cd7cf59b432edcb14810ea5d232ed860d2279318c0c2be7afb4e",
    cases=76, inventory=(1164, 438789, 121, 123), negatives=87801, handoff=True,
)


if __name__ == "__main__":
    main(HANDOFF)
