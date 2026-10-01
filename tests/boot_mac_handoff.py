#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Exact co-owned live RX/AUTOACK handoff; synthetic hardware only."""
from boot_mac_attempt import CALLER_SIZES, MODULES, Profile, main


# radio_autoack complete_head() (stopped drain without FIFOP): +25 CODE,
# relinked identities and +87 required mutations; MMIO trace unchanged.
HANDOFF = Profile(
    stem="mac_handoff_test", prefix="mh", native="host-mac-handoff-tests",
    modules=MODULES[:-1]+("test_mac_handoff",),
    caller_sizes=tuple(CALLER_SIZES.items())+(("clock", 6),),
    size=27998, xdata=1537, stack=0x5c,
    code_sha="2f708167f7eea5f9968a1dc88cffcef0604e97ab01165d1ee4ee8485aabd39bd",
    cdb_sha="c8193501dd10c5c23a43c754fa0d04e495755acea39894796d9f88110b58f13c",
    map_sha="8702ead3ab5fd17c06de66ae8b8c75ca10551334f28481d7bd71716fba67f5eb",
    mem_sha="069a3010d315cd25b4c401b0666dc0757de642715aeef0a5f50613aa5ce508c1",
    list_sha="329a398a65bd168fff473dbbebe0f5ce8f27eb6755c4b997e6b8ae5bae9f756f",
    object_sha="e01fb8a58cfdd7e35df0653b6a59bfea215656b46def36f27b32f6b2a3c70bcc",
    cases=76, inventory=(1164, 438789, 121, 123), negatives=87888, handoff=True,
)


if __name__ == "__main__":
    main(HANDOFF)
