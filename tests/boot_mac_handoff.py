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
    size=28011, xdata=1537, stack=0x5c,
    code_sha="a7daaee4ebe1f60c3dacc63b5f1dbb09d19fc9f48f7ecb248db7fe1715eb7189",
    cdb_sha="484a9233b0c4468f911b2fc253c84c5f2433402b7eb68e90978e6e1bcb9586de",
    map_sha="d5ea6300ace48671e6e111bb9581576d61c4feab8656aa0d99570a6191da2a10",
    mem_sha="e7572294bf2b5d3445e036c7a172cf2a4ea832ffd0d8ac803a7f5f896461735e",
    list_sha="5c52114711453bb76d26d97fe6c8b1e94036ceb43b800570034e95b42513c898",
    object_sha="7e2b1fbd9549f26a5b980c95fb62b4251c95c5bd569831eb98b3914e88fb42e1",
    cases=76, inventory=(1164, 438789, 121, 123), negatives=87888, handoff=True,
)


if __name__ == "__main__":
    main(HANDOFF)
