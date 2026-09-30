#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Exact co-owned live RX/AUTOACK handoff; synthetic hardware only."""
from boot_mac_attempt import CALLER_SIZES, MODULES, Profile, main


HANDOFF = Profile(
    stem="mac_handoff_test", prefix="mh", native="host-mac-handoff-tests",
    modules=MODULES[:-1]+("test_mac_handoff",),
    caller_sizes=tuple(CALLER_SIZES.items())+(("clock", 6),),
    size=27962, xdata=1537, stack=0x5c,
    code_sha="4d71f20c33d2e3fb7f3e47da54833d69c7e4e93ef548cf1fdf5e6ae86df7450c",
    cdb_sha="8d99a07155281bb30610ab2a2b13e24e40a13af15be4f9612dc9cead9ff3ed06",
    map_sha="602ccddc2736446b443f85042e82dba3eac1c7b308ac77e0a630e76494ae8d72",
    mem_sha="2d84dc7ff945bb9b862d0f92f9083445928b789258803b4e7b37a568309630a0",
    list_sha="087fc53299cb37b01faab8dcb62291a217012f0ec037bf3fb8be8acc3887e9b2",
    object_sha="3719b463cf4dfab6734f7e65382c865e05f86e1c31b7d8661bf6dca1dc04ea55",
    cases=76, inventory=(1164, 438415, 121, 123), negatives=87772, handoff=True,
)


if __name__ == "__main__":
    main(HANDOFF)
