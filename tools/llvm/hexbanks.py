#!/usr/bin/env python3
"""Per-bank populated-byte census of a physical CC2530 Intel HEX image.

Physical layout: bank N occupies [N*0x8000, (N+1)*0x8000); bank 0 is the
common/root area; bank 7 ends at 0x3E800 (pages 125-127 reserved for NV/lock).
"""
import hashlib, json, sys

LIMIT = 0x3E800


def load(path):
    mem, base = {}, 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(':'):
            continue
        raw = bytes.fromhex(line[1:])
        assert sum(raw) & 0xFF == 0, 'checksum'
        n, addr, kind, data = raw[0], (raw[1] << 8) | raw[2], raw[3], raw[4:4 + raw[0]]
        if kind == 0:
            for i, b in enumerate(data):
                a = base + addr + i
                assert a not in mem, f'duplicate byte {a:#x}'
                mem[a] = b
        elif kind == 2:
            base = ((data[0] << 8) | data[1]) << 4
        elif kind == 4:
            base = ((data[0] << 8) | data[1]) << 16
        elif kind == 1:
            break
    return mem


def census(path):
    mem = load(path)
    banks = []
    for n in range(8):
        lo, hi = n * 0x8000, min((n + 1) * 0x8000, LIMIT)
        addrs = [a for a in mem if lo <= a < hi]
        top = max(addrs) + 1 - lo if addrs else 0
        banks.append(dict(bank=n, physical_start=lo, capacity=hi - lo, populated=len(addrs),
                          free=hi - lo - len(addrs), extent=top, tail_free=hi - lo - top))
    return dict(hex=path, sha256=hashlib.sha256(open(path, 'rb').read()).hexdigest(),
                populated=len(mem), extent=max(mem) + 1, beyond_limit=sum(a >= LIMIT for a in mem),
                common=banks[0]['populated'], min_headroom=min(b['free'] for b in banks),
                min_tail_free=min(b['tail_free'] for b in banks), banks=banks)


if __name__ == '__main__':
    out = {name: census(path) for name, path in (a.split('=', 1) for a in sys.argv[2:])}
    json.dump(out, open(sys.argv[1], 'w'), indent=1)
    for name, c in out.items():
        print(f"{name:8} sha={c['sha256'][:12]} populated={c['populated']} common={c['common']} "
              f"extent={c['extent']:#x} min_free={c['min_headroom']} min_tail={c['min_tail_free']} "
              + ' '.join(f"{b['bank']}:{b['populated']}/{b['free']}" for b in c['banks']))
