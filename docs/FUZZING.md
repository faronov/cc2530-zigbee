# Parser fuzzing

[`tests/fuzz_parsers.c`](../tests/fuzz_parsers.c) fuzzes the parsers that see
bytes from the air: the MAC frame (both receive profiles), MAC command and
beacon payloads, the NWK beacon payload, NWK and APS headers, the ED wire
syntax and the NWK/APS security auxiliary header (`ed_wire_decode`,
`ed_wire_inspect`, `zigbee_security_inspect`), ZCL frames and values of every
type, ZDO node requests/responses and the stateless ZDO server
(`zdo_srv_handle`). The first input byte selects the parser; the rest is the
frame, copied into a private buffer so that a read past its length reaches the
sanitizer. Every successful decode must describe bytes inside its input
(payload offsets and lengths, consumed prefixes, reply lengths); a violation
aborts.

Before authentication a received frame reaches only the MAC, beacon and
security-header parsers; NWK/APS/ZCL/ZDO payloads are parsed after CCM*
authentication, so only network members can supply them. `ed_wire_crypt`
(MIC verification) is not fuzzed: a random input fails the MIC.

| Command | What it does |
| --- | --- |
| `make test-fuzz-replay` | Host compiler with ASan/UBSan replays the committed corpus [`tests/fuzz/parsers.hex`](../tests/fuzz/parsers.hex) (also run in CI) |
| `make fuzz-parsers [FUZZ_CC=clang] [FUZZ_TIME=60] [FUZZ_SEED=1]` | Unpacks that corpus into `$(BUILD)/fuzz-seeds` and runs libFuzzer for `FUZZ_TIME` seconds into `$(BUILD)/fuzz-corpus` (CI runs 120 s) |

`FUZZ_CC` must be a clang with libFuzzer (`-fsanitize=fuzzer`). The corpus is
text (one input per line, hex) because the repository admits no binary assets.
To extend it with new coverage, merge into a fresh directory and rewrite it:

```sh
B=build/lg_esl29_rev03/join
mkdir -p $B/fuzz-merged && $B/host-parser-fuzz -merge=1 $B/fuzz-merged $B/fuzz-seeds $B/fuzz-corpus
(echo '# Parser fuzz corpus (docs/FUZZING.md): one fuzzer input per line, hex.'
 for f in $B/fuzz-merged/*; do xxd -p -c 4096 $f; done | sort) > tests/fuzz/parsers.hex
```

The corpus holds only fuzzer-generated inputs, never captured network frames
(those carry real addresses, PAN identifiers and counters).

Results on 2026-11-25 (clang 18.1.8, ASan/UBSan): 2-minute and 10-minute runs
without a crash or invariant failure; the decode functions reach 92–100 % of
their lines except `zcl_value_decode` (77 %) and `zdo_srv_handle` (80 %).
