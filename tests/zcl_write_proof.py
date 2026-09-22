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
        23541, "069c28cf769b4102e7c9c9be17614b5eef5e40cc9855948262a2004e42f20fec",
        "7ba8865ee201bde856b66b1e410e0f472a1020a21a6da49f190eddadcc0e87eb",
        "0bfe7a073ff55909555e8a5fe1d9b56cd2198244c9ee8391be7f0818f1131f94",
        {
            "mac_frame": (4253, 7136, "645aa254a949c4cab3fae1cb2a020da4ced39c58b184200992c6978698177236", "38e7d35fca29790c52cf6d80d73605a073e544774ff6f96b951f2abf7fef191d"),
            "nwk_frame": (1344, 2294, "ac851c90cc185371a3c63423e99ab88b1404dd678f880e51fc70332963ffb59c", "6a1ec462fa361f74b96df643510821afd5aed3be1d87eb123a485c509c25495a"),
            "aps_frame": (939, 1626, "fcd530ca7240bee705e7bd48191677b85d5aaabcbca8a8b1b03298b29f00c724", "948f1fe7b92ee66bfadf646dfc43303bde92b6554849141c01ca571f7912f5bb"),
            "zcl_frame": (663, 1173, "25f8362fca81ccb0f92b9826c0c4f709198c2cc2c325b30d4f74ad7c6bf93b0f", "f837980f6adb7e3a899916cd5a8201f739f84fa221c23698b22ef8d25b93aae5"),
            "zcl_value": (768, 1345, "cdf253131214b9c0614f8d603b004a7869d327584d1c6620224c74229ef020ed", "996d270d11918dac366a4de4026d70b1ba017dc5822cba17012fba7ca2b7e4b6"),
            "zcl_attributes": (1217, 1958, "74761480dac22b85e59ea52709c4e6ccbc3e1950676fa4b903699a8bb5d0df1d", "d99d799854c7b7ff4e9071775d58b02c1dbfa7a3b8ec59376b490e049a3a9714"),
            "zcl_dispatch": (1702, 2725, "37445a56824c1353f16869969d673f55be44d4a765529cff5b7d2a3ddd48378c", "bf65a805ca7453fc605fffa1a18e4a7795276d2738a931724fab542bef8f141e"),
            "zcl_write": (1016, 1552, "b01651813ed1f82f9eae708ff697ff280add267b2dcee512b34fcdd02b3906e9", "6777f8713a868f7c71a6b87325dc126c807c1c8f5f9951b7525432483e2b1c99"),
            "protocol_budget_test": (1637, 2946, "e5ba747fe49861a2f4b54b59003c1ce67c713d6dbbde102304bef9bc04e6a3be", "dc95c6bd4f3bc4adecd5c6f7c69339e12cef51e6a229e0556745552333d2fafb"),
        },
    ),
}
AREAS = dict(AREAS_SHA, **{
    "zcl_dispatch_test": "b04346048cd7e111530b73f608cb92bd4f4d218f8f5c6b37f31d0afa3a3d7647",
    "protocol_budget_test": "20f041fc2abdf3bb83a5b6c78a5b4dcab9c07c428c8e8b10462f6f0f9f6b7a0c",
    "mac_frame": "a663c38f9ba25e4ccd7cc99303005166d82eea4e615f79c24c3bd4cc3bedf391",
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
