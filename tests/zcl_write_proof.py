# SPDX-License-Identifier: BSD-3-Clause
"""Complete identities for the write-coupled dispatch and resource images."""

import re
import unittest

from boot_nwk_candidates import listing_metrics, records
from boot_zcl_basic import AREAS_SHA, canonical, sha, verify_write_abi
from verify_firmware import cdb_address, code_bytes, require


# Reviewed actual SDCC links; never generated or learned by the verifier.
# Per-module tuples: instruction count, byte count, ordered-instruction SHA,
# complete raw immediate relocated listing SHA.
PINS = {
    "zcl_dispatch_test": (
        18596, "704809355f0314375f3ca9a87a21a57b5442cefb967afcec9013be0affabc4a0",
        "b87fbb55b6df58664eac27a98e7306a738672e2455fa7504366d735998d9d61d",
        "b5e797286dc0a7bb879b7c081ef29b00f3807d294063920e7166ef99f36cfcf0",
        {
            "zcl_dispatch": (1942, 3093, "dd4c74d59bf42ef696501965f116efcda23cce97e887489c5c3a028a9f0aaf0a", "56c291299c1df132b0aa551d47759ce12e312cf71d6f61ade2e4e78323a48e70"),
            "zcl_write": (1016, 1552, "30da5da51a8d46be3a80e9587049f0d4756ceb533c83d07779874e57cf1dd114", "34d3f69b84b086761e258b9d407a8c2518e996ee15e6cd71a00a9e13835d897f"),
            "zcl_attributes": (1284, 2086, "2cc4fd07e90cb429fe7a02a4175dbe06bc3ff5e825a8683765e78cf3366fa8dd", "11b18d9e361edf7610c88208abff49611553b5ddd20c444e2b06178bd8ec6d0f"),
            "zcl_frame": (663, 1173, "4e3f4374df0fd713f260a0a1424dd708d6c67bf6b7d97ecdbc46f28cf37fcbe1", "c0778556738722b3a70028e5f7232f860b1713defa95d6404f1cc050ef40aed8"),
            "zcl_value": (1006, 1710, "cf3e7af50ba375e5509f5b18dc54e441d9d4811fb0afb2881c5659779c43504a", "4fa341c2eb2d6d1372be94316734cba26ddf071373efa1652167cd0671607e8e"),
            "zcl_dispatch_test": (4500, 8169, "1aeb75d1be0e663bd243c15d317a7c5ac2c5245713659241f7fa8f49b225d335", "f5073fc1dde8b6602c297e307430edb140351914ac418462c42bc9df73ff10f4"),
        },
    ),
    "protocol_budget_test": (
        24575, "a38512f1efff97979883e1392db3a35a97c9c207d5b364209852906b7398c209",
        "3303148c4fb56fc0abdcd64f7e4e3f561cd74d19ad28f5217ecd364be4a170d7",
        "d974046c31fc1fcb02b04b52a2df4ff685dbea0568d0b0dd285b84460b737c47",
        {
            "mac_frame": (4253, 7136, "b0e431e85f6f3a34bed3629f1d42b6e827eae74ff22f79b30e6c8ce6ae6f13c2", "23e8cb77ea9496f85be1538bb99803413bd2903ff4b7e98d31b7b0a09783bc84"),
            "nwk_frame": (1344, 2294, "6cc3aaee32ed144622b997343852a9797c901959bf52408bf3a463d2974a9396", "f7855eca0ea1b5390800a684df028cfa6d1d8c2a8fb8c8490475c390b88d4c97"),
            "aps_frame": (939, 1626, "98d511c0a4d398c1f9090420080bf594d4699b3de0f2aafe69e45397d997d5b7", "5e01ea77e62d99ee3df0edf138c83da77b54883debfde41d7c087efa940375be"),
            "zcl_frame": (663, 1173, "4ff8b03de7f0da06b3505e972781cd88fb460c26fb51456b117cfb9d199cbc9c", "5fde8ee3a60079fd7f3999ae28ad4020895ba67dc1902b06d20bef97f977ca63"),
            "zcl_value": (1006, 1710, "6e10d94b93dd82b9d3ea2156b5cacf2706ae9730a14bb29939c5e8a1f2251354", "4d9872411e3ea0679cbc4f70364afad16e4f77efc238a31edf75416d286e2b14"),
            "zcl_attributes": (1284, 2086, "d4b52eb72731f4f0c5c0e03e708883e087c4ec3408ec86d9615072cfc54ce6d7", "31b6b1ea32db2bc60a74af20a8b6a0889163bda28e5877858c2bd876d17d8172"),
            "zcl_dispatch": (1942, 3093, "1015b4637ba0e6d13b92d4f48a2fe89d482f1fc8a2be415101db0f95a97638b2", "a07c41a3b2b60c0fd0a58accc788113bd9060cb8598e49300281c5f9bba3eb35"),
            "zcl_write": (1016, 1552, "33c2596651f363ef71560be1a7eeb3a533e5dc30ac313f9980f66b69832ee5d2", "a334fe4d3d351202138146ccd910f6518759a8caf30d8bc29fd2cf6ccee554d6"),
            "protocol_budget_test": (1637, 2946, "a05219f11246424a5b0012315442428bae3d7b3bcd8141e62fc693b9dabfce7b", "a51e0aba123691dfe25213aa3f33dbe30b5614a49bc06628a7c758d0cd958057"),
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
    for prefix in (b"F:", b"S:", b"L:", b"T:", b"F:G$zcl_wr_handle", b"S:Lzcl_write", b"L:C$"):
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
