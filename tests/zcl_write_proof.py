# SPDX-License-Identifier: BSD-3-Clause
"""Complete identities for the write-coupled dispatch and resource images."""

import re
import unittest

from boot_nwk_candidates import listing_metrics, records
from boot_zcl_basic import AREAS_SHA, canonical, sha, verify_value_abi, verify_write_abi
from verify_firmware import cdb_address, code_bytes, require


# Reviewed actual SDCC links; never generated or learned by the verifier.
# Per-module tuples: instruction count, byte count, ordered-instruction SHA,
# complete raw immediate relocated listing SHA.
PINS = {
    "zcl_dispatch_test": (
        17735, "f875dc310610618466a4c5d808c29fabdc81c0846f7b05db7a02ec4fec33d776",
        "d1728c911950d5730c4432520cabe99e0b29d9a4ecf48c0a3afc06cfb0244421",
        "9ec6c13253931717d430e91d09dcc0dfa78f86bc94107ec608fe39b6c2f1e256",
        {
            "zcl_dispatch": (1702, 2725, "9d82c8b11c3f56e6b6c66ead394fc3e3ed3108cdcee52272ee26c43d0b3e6a0c", "c7cd73f3615cec59aa0f4b1f518c6879176b01844c127fd819082569ed5c1cd2"),
            "zcl_write": (1016, 1552, "da47879d71e1b352c02ecd4db4be138eda19fedb2beb61a978f7bb4ebd16a0d0", "a87c156b1ef043877ebed55499ced4ae2fa24830b57ef0bee4f6218d56726b8b"),
            "zcl_attributes": (1217, 1958, "ca882a7d008264d8b004425d7819ef5803e8f3e2c1f04bb41f97d647e32c55fe", "b0710c6e40a6759ca3cebe0ffb7bc483d061835635f8a9451dc6e24f3523a415"),
            "zcl_frame": (663, 1173, "986db5405087b7f91e087ddba7a14ef004e9ae66d5c939367f103110b86faca6", "5d8fd014105263f2c4fe0249b1a352ad8ef0c159ebc0f8d916235ae5c5f6d48e"),
            "zcl_value": (768, 1345, "83d89315cc666e7b768846c9da7050a608770def643356078bd06dd22c17e1a8", "0f40b16a9e1fed1fb5cc3a7b03ce7696893ce0b4e6dc031a37aa1a36b14f6fbe"),
            "zcl_dispatch_test": (4500, 8169, "c40da63bdf4965ed1fe60a349cd106804fe6cf747249a0d78379ba58f42b94ee", "30fd88f2ee21d89fd1517b6c0bde1ac8bdf6388349bdf9c5950dfa9425cd063d"),
        },
    ),
    "protocol_budget_test": (
        23498, "d7c07e672b5f998866fc05ef217d71551f544f039f6b18f2d38436957ba6f7fe",
        "339a75594bb1ff41161866f0067780f3515412e225ec14069c089db7d39e0bec",
        "21f8603d939627cb21cf4875ff1a1ce4d8b2d53a8e9db47e00b619a6c80aa401",
        {
            "mac_frame": (4262, 7093, "67e06e1521a24f6505a827d473dc161a74f70568f9f77809edd1caead91cd40d", "6f0d67c2950eece2dc8127c3ccd2cc56da087967fd3889a8aac71157725edfe4"),
            "nwk_frame": (1344, 2294, "c13c8f3cf656311665dc4ed6b0492800774105184699f233431ee47b6a055530", "d128a12e0eb8ef332f3ca72617a37cd0377a8744546e0791df770677e2af691e"),
            "aps_frame": (939, 1626, "d4a5d180372761b97d551e28ad7c6a1fd71d08d6c4f733ae3c4098f8d845616b", "7520564e5dd30949df35640d781d5e558d09ff99c6c92b689ef90a933cef3e4d"),
            "zcl_frame": (663, 1173, "ad23b32302d68cc0b634bc073609218b14a121e754cfc46d530bf0a1fe58e977", "7d1e5e577b09f5b6e7ffa2beab489f0dda1b719a787ed644e1e176fd68d50ad7"),
            "zcl_value": (768, 1345, "09f274ecb840d01c187355309d0ddbce86ac1f1d1a1ffa2b6f05bcdce4b50ba5", "87cf1888a8cb3ef91d341bcf26992e26600ca45c9c28ca0a8e42eed2827e2b39"),
            "zcl_attributes": (1217, 1958, "04444399659d6921cb00ead047f5ab0009261a6c8910599f15fa864b18369a5f", "3888878deaa8c00467ec0ea067f6b604d9ad6ceb2c4f33857fe05274b9645931"),
            "zcl_dispatch": (1702, 2725, "b4ee37fcfd95b91d4dff0e6eace2bc455e37662c0d2025f577fa11a55af482e6", "78ac87502c5fe3b08ecd0e5b0f10c930ec332d333d577ab017c8f171a9f08928"),
            "zcl_write": (1016, 1552, "ed35d7d464ee24b7484692d29423838245e3c71fb30c500d4902a8b393582062", "118f35e436d76ba061430513d4d3ff20994d331eae9b8020ada32c4c6d384f70"),
            "protocol_budget_test": (1637, 2946, "53ec1fb2c31b988afe5a25466d2fd99673af754e1f1380f06ff09803825e8c83", "700406faa65496b5c2dab59a1bee32c72e60676269322815aedc7eb12b289e64"),
        },
    ),
}
AREAS = dict(AREAS_SHA, **{
    "zcl_dispatch_test": "b04346048cd7e111530b73f608cb92bd4f4d218f8f5c6b37f31d0afa3a3d7647",
    "protocol_budget_test": "20f041fc2abdf3bb83a5b6c78a5b4dcab9c07c428c8e8b10462f6f0f9f6b7a0c",
    "mac_frame": "d6a674a314b9d875017929cf84773f5c9d38adf4c7b454bdde1b555c5a1b6a87",
    "nwk_frame": "eefeaaa855e7260fb922b9bb3a56cef176b0264328e5fdc797dbc23c51098225",
    "aps_frame": "9bae158118b3ee711ba420e1c88800e25dd978e486a0567841cdd1a495c2f727",
})


def verify(stem, image, symbols, raw, listings, objects):
    size, code_sha, cdb_sha, map_sha, modules = PINS[stem]
    require(isinstance(raw, bytes) and sha(raw) == cdb_sha, "Write composition raw CDB changed before decode")
    debug = raw.decode("ascii")
    require(sha(code_bytes(image, size)) == code_sha, "Write composition complete CODE changed")
    require(sha(canonical(symbols)) == map_sha, "Write composition complete map changed")
    require(set(listings) == set(objects) == set(modules), "Write composition module set changed")
    coverage, instructions = set(), {}
    for m, expected in modules.items():
        require(sha(listings[m]) == expected[3], f"Write complete immediate listing changed: {m}")
        text = listings[m].decode("ascii")
        require(listing_metrics(text) == expected[:3], f"Write ordered instructions changed: {m}")
        areas = re.findall(r"^A (\S+) size (\S+) flags (\S+) addr (\S+)$", objects[m], re.MULTILINE)
        require(sha(canonical(areas)) == AREAS[m], f"Write exact module allocation changed: {m}")
        for address, data in records(text):
            span = set(range(address, address + len(data)))
            require(not coverage & span and all(image.get(address+i) == b for i, b in enumerate(data)),
                    "Write duplicate/overlapping/non-linked instructions")
            coverage.update(span)
            instructions[address] = data
    verify_write_abi(debug, symbols, instructions)
    verify_value_abi(debug, instructions)
    lo = cdb_address(debug, "L:G$zcl_dispatch_unicast$0$0")
    hi = cdb_address(debug, "L:XG$zcl_dispatch_unicast$0$0")
    require(any(lo <= a <= hi and data == b"\x12" + symbols["_zcl_wr_handle"].to_bytes(2, "big")
                for a, data in instructions.items()), "Dispatcher bypasses actual write handler")
    return debug


def load_and_verify(output, stem, image, symbols):
    """Identity checks and rejection controls; no extra simulator invocation."""
    raw = (output / f"{stem}.cdb").read_bytes()
    modules = PINS[stem][4]
    listings = {m: (output / f"{stem}.{m}.rst").read_bytes() for m in modules}
    objects = {m: (output / f"{m}.rel").read_text(encoding="ascii") for m in modules}
    debug = verify(stem, image, symbols, raw, listings, objects)
    case = unittest.TestCase()

    def reject(**changes):
        args = dict(stem=stem, image=image, symbols=symbols, raw=raw, listings=listings, objects=objects)
        args.update(changes)
        with case.assertRaises(ValueError):
            verify(**args)

    for address in (0, PINS[stem][0]-1, symbols["_zcl_wr_handle"], symbols["_main"]):
        reject(image=image | {address: image[address] ^ 1})
    reject(image=image | {PINS[stem][0]: 0})
    reject(symbols=symbols | {"unreviewed": 1})
    for name in ("s_SSEG", "l_XSEG", "_zcl_wr_handle_PARM_7"):
        reject(symbols=symbols | {name: symbols[name]+1})
    for prefix in (b"F:", b"S:", b"L:", b"T:", b"F:G$zcl_wr_handle", b"S:Lzcl_write", b"L:C$",
                   b"F:Fzcl_value$value_shape", b"S:Lzcl_value.non_value_pattern"):
        lines = [line for line in raw.splitlines(keepends=True) if line.startswith(prefix)]
        require(lines, f"Missing write metadata negative prerequisite: {prefix!r}")
        reject(raw=raw.replace(lines[0], b"", 1))
        reject(raw=raw + lines[0])
    reject(raw=raw + b"\xff")
    reject(raw=raw.replace(b"\n", b"\r\n"))
    for m in modules:
        reject(listings=listings | {m: b""})
        lines = listings[m].splitlines(keepends=True)
        a, b = [i for i, line in enumerate(lines) if records(line.decode("ascii"))][:2]
        changed = lines.copy()
        changed[a], changed[b] = changed[b], changed[a]
        reject(listings=listings | {m: b"".join(changed)})
        reject(listings=listings | {m: b"".join(lines[:a] + lines[a+1:])})
        reject(listings=listings | {m: b"".join(lines[:a] + [lines[a]] + lines[a:])})
        reject(objects=objects | {m: objects[m].replace("A XSEG size ", "A ISEG size ", 1)})
    return debug
