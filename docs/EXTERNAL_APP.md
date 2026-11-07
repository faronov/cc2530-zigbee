# External applications

An external application is firmware for another product, kept in its own
repository. It reuses this stack's join, security, NWK/APS and READY engine
unchanged. It replaces only the application server (`zcl_sensor`) and the
board file. It includes this repository as a git submodule. The first such
application is the FlowerX soil sensor,
[faronov/flower-cc2530](https://github.com/faronov/flower-cc2530).

Nothing in this repository enables an external application by default. The LG
ESL29 and generic images are byte-identical with or without these hooks.

## Interface

`CC2530_APP` selects the external application. With it, the stack includes two
headers from the application's include directory:

- `app_config.h` is included by `cc2530_mmio.h`. It holds macros only, with no
  includes.
  - With `CC2530_BOARD=2` (`BOARD_EXTERNAL`) it must define
    `BOARD_MANUFACTURER`, `BOARD_MODEL` and `BOARD_MODEL_LENGTH` (the ZCL Basic
    identity).
  - It may define `BOARD_CCA_THRESHOLD_RAW` (raw CCACTRL0; the default is
    `0xF8`).
  - It may define `CC2530_APP_REGISTER_LIST(X)`, which lists SFRs used only by
    the application in the form `X(SOC_NAME, address)`. They extend the shared
    MMIO list in that image only, so the simulator's peripheral ownership
    (`tests/boot_join_smoke.py`) does not change for any other image.
- `app_zcl.h` is included by `zcl_sensor.h` instead of the built-in server's
  definitions. It must provide:
  - `ZCL_SENSOR_STATE`, an lvalue with a `sent` flag set by the caller
    (`ZCL_SENSOR_REPORTS` and `zcl_sensor_state_t`, which `zcl_sensor.h`
    declares only for the built-in server);
  - every `zcl_sensor_*` entry point declared in `zcl_sensor.h`;
  - `zcl_sensor_boot()`, called once by `examples/join_smoke_main.c` before
    admission;
  - with `CC2530_APP_QUIET`, `zcl_sensor_quiet()`, which bounds the PM2 sleep
    span and returns 0 to keep the device awake (sleep reason 5).

The board file must define `board_description` and `board_early_off()`.
`board_early_off()` runs from startup before C data initialization, so it may
use direct MMIO only.

The stack options remain ordinary defines that the application chooses, for
example:

- `CC2530_ROUTER_JOIN` ([ED_JOIN.md](ED_JOIN.md#router-parent-initial-join-cc2530_router_join));
- `CC2530_ZDO_DESCRIPTORS` (`zdo_srv_descriptors[]`, see `zdo_srv.h`);
- `CC2530_AUTONOMOUS`, `CC2530_LEAN`, `CC2530_RX_OFF_IDLE` and
  `CC2530_PM2_IDLE` (see [M7_SLEEP.md](M7_SLEEP.md)).

## Manifest and LLVM build

The application root holds `cc2530-app.json`:

```json
{
  "schema": "cc2530-app/1",
  "name": "flower",
  "defines": ["-DCC2530_PARENT_RECOVERY", "-DCC2530_ROUTER_JOIN", "..."],
  "include": ["include"],
  "units": {"zcl_sensor": "src/zcl_flower.c", "lg_esl29_rev03": "boards/flower_v2.c"}
}
```

- `defines` must be plain `-DNAME[=VALUE]` or `-UNAME` flags.
- `CC2530_APP` and the board number are added by the build, not by the
  manifest.
- `units` replaces stack translation units from
  `tools/llvm/inputs/srcmap-stk.txt` with tracked application files. The object
  count, link, static-frame overlay and admission are unchanged.

Build it with:

```sh
python3 cc2530-zigbee/tools/llvm/firmware.py --tools <llvm>/bin --llvm-src <llvm-project> \
  --output build/firmware --board external --image join --variant app --app .
```

`build.sh` copies the application's git-tracked files into `src/app` of the
build snapshot. A submodule stays an empty directory. The build compiles with
`-Iapp/<include>` and records the substituted `srcmap.txt`, which `audit.sh`
and `sframe_link_hw.py` follow. The receipt identity adds the manifest and the
application's tree identity. The LG `sensor_layout.py` harness is skipped
because the application owns its own state.

## Host tests

Run the host tests from the application:

```sh
make -C cc2530-zigbee test-app-host APP_DIR=$PWD BUILD=$PWD/build/host \
  APP_HOST_TEST=tests/test_app.c APP_HOST_DEFINES="-DCC2530_APP_QUIET"
```

This builds the test with the join-smoke host flags, `CC2530_APP` and
`CC2530_BOARD=2`, plus `-I<app>/include -I<app>/src`. It runs both the plain
binary and an ASan/UBSan binary.
