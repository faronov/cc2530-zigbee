#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Exact co-owned live RX/AUTOACK handoff; synthetic hardware only."""
from boot_mac_attempt import CALLER_SIZES, MODULES, Profile, main


HANDOFF = Profile(
    stem="mac_handoff_test", prefix="mh", native="host-mac-handoff-tests",
    modules=MODULES[:-1]+("test_mac_handoff",),
    caller_sizes=tuple(CALLER_SIZES.items())+(("clock", 6),),
    size=27423, xdata=1543, stack=0x5c,
    code_sha="dbf1a0c52e734fe6ced409d2803c95071483fb841ad1b3b6f96276b8f6ac155b",
    cdb_sha="b171cada2f20a2acb576d70412e87635774d28780286ec86dc0d88e032ff364d",
    map_sha="8109ec57d792f75040e97b95ee42b61b34daf700c6a056c9dc5bc9fa84996fca",
    mem_sha="2888fc77d37261c0d98b43c114f27ed21fe294cc1df656927a745d257595bcf0",
    list_sha="0f14a2200973e7df3a853f31ba964c0556b4d243d3db11a3db7aa9ce2da0dd34",
    object_sha="fefcee908351746672c0b8e213d01b60d706990ac836b0b6fd162a453a8cc824",
    cases=71, inventory=(1099, 409315, 121, 123), negatives=86410, handoff=True,
)


if __name__ == "__main__":
    main(HANDOFF)
