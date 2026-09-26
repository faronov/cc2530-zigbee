# SPDX-License-Identifier: BSD-3-Clause
"""Make orchestration coverage without building, simulating or accessing USB."""
from collections import Counter
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

from verify_firmware import BOARDS, IMAGES, ROOT
from ci_plan import COMPONENTS, recipe
from link_ram_resources import MODULES as LINK_RAM_MODULES
from link_ram_resources import WORKSPACE_MODULES as LINK_WORKSPACE_MODULES


@unittest.skipUnless(shutil.which("make"), "GNU Make unavailable")
class LocalChecksTests(unittest.TestCase):
    def dry_run(self, *targets, include_build=False, **variables):
        with tempfile.TemporaryDirectory(prefix="cc2530-make-test-") as directory:
            environment = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL")}
            result = subprocess.run(
                ["make", "--no-print-directory", "-n", "-j1", "PYTHON=python3",
                 f"BUILD={directory}/single", f"LOCAL_BUILD={directory}/matrix",
                 *(f"{key}={value}" for key, value in variables.items()), *targets],
                cwd=ROOT, env=environment, capture_output=True, text=True, check=True, timeout=30,
            )
            self.assertEqual(list(Path(directory).iterdir()), [])
            return [shlex.split(line) for line in result.stdout.splitlines()
                    if line.startswith(("python3 ", directory + "/")) or
                    include_build and line.startswith(("cc ", "sdcc ", "cp "))]

    def test_interval_profile_keeps_native_sanitizer_radio_and_target_proofs(self):
        for board in BOARDS:
            commands = self.dry_run("test-mac-tx-interval", BOARD=board, include_build=True)
            hosts = [args for args in commands if args[0] == "cc"]
            self.assertEqual(len(hosts), 4)
            self.assertTrue(all("-DCC2530_MAC_INTERVAL" in args for args in hosts))
            self.assertEqual(sum("-fno-sanitize-recover=all" in args for args in hosts), 2)
            radio = [args for args in hosts if "tests/test_mac_attempt.c" in args]
            self.assertEqual(len(radio), 2)
            for args in radio:
                self.assertTrue({"src/mac_tx.c", "src/mac_frame.c", "src/mac_attempt.c",
                                 "src/mac_radio.c", "src/mac_time.c", "src/radio_autoack.c",
                                 "-DCC2530_MAC_RADIO", "-DCC2530_MAC_ATTEMPT"} <= set(args))
            links = [i for i, args in enumerate(commands)
                     if args[0] == "sdcc" and args[args.index("-o")+1].endswith(".ihx")]
            self.assertEqual(len(links), 1)
            self.assertTrue(all(args[0] == "cp" for args in commands[links[0]+1:links[0]+4]))
            self.assertEqual(sum("tests/boot_mac_tx_interval.py" in args for args in commands), 1)

    def test_handoff_keeps_four_native_callers_and_isolated_target_profile(self):
        for board in BOARDS:
            commands = self.dry_run("test-mac-handoff", BOARD=board, include_build=True)
            hosts = [args for args in commands if args[0] == "cc"]
            self.assertEqual(len(hosts), 4)
            for args in hosts:
                self.assertTrue({"-DCC2530_MAC_RADIO", "-DCC2530_MAC_ATTEMPT",
                                 "-DCC2530_MAC_HANDOFF", "tests/test_mac_handoff.c",
                                 "src/mac_attempt.c", "src/mac_radio.c", "src/mac_time.c",
                                 "src/radio_autoack.c", "src/clock.c", "src/timebase.c"} <= set(args))
                self.assertEqual("-DCC2530_MAC_INTERVAL" in args, "src/mac_tx.c" in args)
            self.assertEqual(sum("-DCC2530_MAC_INTERVAL" in args for args in hosts), 2)
            self.assertEqual(sum("-fno-sanitize-recover=all" in args for args in hosts), 2)
            compiles = [args for args in commands if args[0] == "sdcc" and "-c" in args]
            self.assertEqual(len(compiles), 8)
            self.assertTrue(all("-DCC2530_MAC_HANDOFF" in args and "-DCC2530_MAC_INTERVAL" not in args
                                for args in compiles))
            self.assertEqual(sum("tests/boot_mac_handoff.py" in args for args in commands), 1)

    def test_bdb_object_layout_is_mandatory_but_not_an_executable_claim(self):
        for board in BOARDS:
            commands = self.dry_run("test-bdb-join", BOARD=board, include_build=True)
            compiles = [args for args in commands if args[0] == "sdcc"]
            self.assertEqual({args[args.index("-c")+1] for args in compiles},
                             {"src/nwk_aps.c", "src/nwk_aps_transmit.c", "src/zdo_runtime.c",
                              "src/bdb_join.c", "src/bdb_join_init.c", "tests/bdb_join_layout.c"})
            self.assertTrue(all("--model-large" in args and "--std-c99" in args and
                                args[args.index("-o")+1].endswith(".rel") for args in compiles))
            self.assertFalse(any("tests/boot_" in arg for args in commands for arg in args))
            self.assertEqual([Path(args[0]).name for args in commands if args[0] not in ("cc", "sdcc")],
                             ["host-bdb-join-tests", "host-bdb-join-tests-sanitize"])

    def test_adapter_keeps_real_banked_services_six_native_builds_and_immediate_snapshots(self):
        modules = ("mac_adapter_iram_low", "banked", "mac_adapter_iram_high", "timebase", "clock",
                   "mac_time", "radio_autoack", "mac_epoch", "mac_radio", "mac_attempt", "mac_frame",
                   "mac_tx", "mac_adapter", "mac_adapter_fixture")
        for board in BOARDS:
            commands = recipe(board, "mac-adapter")
            compiles = [args for args in commands if args[0] == "sdcc" and "-c" in args]
            self.assertEqual(tuple(Path(args[-1]).stem for args in compiles), modules)
            for args in compiles:
                self.assertTrue({"--model-large", "--debug", "--Werror", "-DCC2530_BANKED_MAC",
                                 "-DBANKED_STACK_FIRST=0x56"} <= set(args))
                self.assertFalse({"--model-huge", "--stack-auto", "--xstack",
                                  "--parms-in-bank1", "-DCC2530_HOST_TEST"} & set(args))
            link = next(args for args in commands if args[0] == "sdcc" and "-c" not in args)
            self.assertEqual(tuple(Path(arg).stem for arg in link if arg.endswith(".rel")), modules)
            for option, expected in (("--stack-size", "0x27"), ("--xram-size", "0x1e00"),
                                     ("--code-size", "0x80000")):
                self.assertEqual(link[link.index(option)+1], expected)
            for bank in (1, 2):
                self.assertIn(f"-Wl-bMA_BANK{bank}=0x{bank}8000", link)
            directory = Path(link[link.index("-o")+1]).parent
            self.assertEqual(commands[commands.index(link)+1],
                             tuple(arg for module in modules for arg in
                                   ("cp", str(directory/f"{module}.rst"), str(directory/f"mac_adapter.{module}.rst")+";")))
            headers = [args for args in commands if "tests/verify_mac_adapter.py" in args]
            self.assertEqual(len(headers), 1)
            self.assertIn("--emit-header", headers[0])
            native = [args for args in commands if args[0] == "cc"]
            self.assertEqual(len(native), 6)
            self.assertEqual(sum("-fno-sanitize-recover=all" in args for args in native), 3)
            self.assertEqual(sum("-DMAC_ADAPTER_TRACE" in args for args in native), 2)
            self.assertTrue(all("-DCC2530_BANKED_MAC" not in args and "-DNDEBUG" not in args for args in native))
            self.assertEqual(sum("tests/boot_mac_adapter.py" in args for args in commands), 1)
            for args in recipe(board, "bringup"):
                self.assertFalse(any("mac-adapter" in arg or "-DCC2530_BANKED_MAC" == arg for arg in args))

    def test_reconfiguration_gates_only_its_own_objects_and_native_builds(self):
        gated = ("radio_autoack", "mac_radio", "mac_attempt", "mac_adapter")
        for board in BOARDS:
            commands = recipe(board, "mac-reconfig")
            compiles = [args for args in commands if args[0] == "sdcc" and "-c" in args]
            reconfig = [args for args in compiles if "-DCC2530_MAC_RECONFIG" in args]
            self.assertEqual(tuple(Path(args[-1]).stem for args in reconfig), gated)
            for args in reconfig:
                self.assertEqual(Path(args[-1]).parent.name, "mac-reconfig")
                self.assertTrue({"--model-large", "--debug", "--Werror", "-DCC2530_MAC_ADAPTER",
                                 "-DCC2530_BANKED_MAC", "-DBANKED_STACK_FIRST=0x56"} <= set(args))
            self.assertTrue(all(Path(args[-1]).parent.name == "mac-adapter"
                                for args in compiles if args not in reconfig))
            native = [args for args in commands if args[0] == "cc"]
            self.assertEqual(len(native), 2)
            self.assertTrue(all("tests/test_mac_reconfig.c" in args and "-DCC2530_MAC_RECONFIG" in args and
                                "-DMAC_ADAPTER_TRACE" not in args and "-DCC2530_BANKED_MAC" not in args
                                for args in native))
            self.assertEqual(sum("-fno-sanitize-recover=all" in args for args in native), 1)
            runs = [Path(args[0]).name for args in commands if args[0] not in ("cc", "sdcc", "mkdir", "cp")
                    and not any("verify_mac_adapter.py" in arg for arg in args)]
            self.assertEqual(runs, ["host-mac-reconfig-tests", "host-mac-reconfig-tests-sanitize"])
            self.assertFalse(any("tests/boot_" in arg for args in commands for arg in args))
            for unit in ("mac-adapter", "bringup"):
                self.assertFalse(any("-DCC2530_MAC_RECONFIG" in args for args in recipe(board, unit)))

    def test_interval_consumers_gate_only_their_own_objects_and_native_builds(self):
        gated = ("mac_tx", "mac_scan", "mac_poll", "mac_association", "mac_join",
                 "nwk_aps", "nwk_aps_transmit", "zdo_runtime", "bdb_join", "bdb_join_init")
        driver = gated + ("mac_link_driver",)
        banks = {"mac_scan": "BJ_BANK3", "bdb_join": "BJ_BANK3", "bdb_join_init": "BJ_BANK3",
                 "nwk_aps": "BJ_BANK4", "nwk_aps_transmit": "BJ_BANK4", "zdo_runtime": "BJ_BANK4",
                 "mac_link_driver": "BJ_BANK4"}
        full = {"-DCC2530_MAC_RADIO", "-DCC2530_MAC_ATTEMPT", "-DCC2530_MAC_HANDOFF",
                "-DCC2530_MAC_ADAPTER", "-DCC2530_MAC_RECONFIG"}
        for board in BOARDS:
            commands = recipe(board, "mac-link")
            compiles = [args for args in commands if args[0] == "sdcc" and "-c" in args
                        and "-DCC2530_MAC_LINK" in args]
            self.assertEqual(tuple(Path(args[-1]).stem for args in compiles), gated + driver)
            for index, args in enumerate(compiles):
                stem = Path(args[-1]).stem
                self.assertEqual(Path(args[-1]).parent.name, "mac-link" if index < len(gated) else "mac-link-driver")
                self.assertTrue({"--model-large", "--debug", "--Werror", "-DCC2530_BANKED_JOIN",
                                 "-DCC2530_MAC_INTERVAL", "-DCC2530_MAC_OBSERVED",
                                 "-DCC2530_MAC_LINK"} <= set(args))
                self.assertEqual(full <= set(args), index >= len(gated))
                self.assertNotIn("-DCC2530_BANKED_MAC", args)
                self.assertEqual(args[args.index("--codeseg") + 1], banks.get(stem, "BJ_BANK2"))
                self.assertEqual(args[args.index("--dataseg") + 1], "BJ_" + stem)
            self.assertTrue(all(Path(args[-1]).parent.name == "mac-adapter" for args in commands
                                if args[0] == "sdcc" and "-c" in args and args not in compiles))
            native = [args for args in commands if args[0] == "cc"]
            self.assertEqual(len(native), 6)
            self.assertTrue(all({"-DCC2530_MAC_INTERVAL", "-DCC2530_MAC_OBSERVED", "-DCC2530_MAC_LINK"}
                                <= set(args) for args in native))
            e2e = [args for args in native if "tests/test_mac_link_e2e.c" in args]
            self.assertEqual(len(e2e), 2)
            self.assertTrue(all(full <= set(args) and "src/mac_link_driver.c" in args
                                and "-DMAC_ADAPTER_TRACE" not in args and "-DCC2530_BANKED_MAC" not in args
                                for args in e2e))
            self.assertEqual(sum("-fno-sanitize-recover=all" in args for args in native), 3)
            runs = [Path(args[0]).name for args in commands if args[0] not in ("cc", "sdcc", "mkdir", "cp")
                    and not any("verify_mac_adapter.py" in arg for arg in args)]
            self.assertEqual(runs, ["host-mac-link-scan-tests", "host-mac-link-scan-tests-sanitize",
                                    "host-mac-link-join-tests", "host-mac-link-join-tests-sanitize",
                                    "host-mac-link-e2e-tests", "host-mac-link-e2e-tests-sanitize"])
            self.assertFalse(any("tests/boot_" in arg for args in commands for arg in args))
            for unit in ("compositions", "bringup", "banked-join-success", "mac-adapter", "mac-reconfig"):
                self.assertFalse(any("-DCC2530_MAC_LINK" in args for args in recipe(board, unit)))

    def test_compact_link_has_complete_objects_without_a_new_image_claim(self):
        for board in BOARDS:
            commands = recipe(board, "mac-link-ram")
            compiles = [args for args in commands if args[0] == "sdcc" and "-c" in args
                        and "-DCC2530_MAC_LINK_RAM" in args]
            self.assertEqual(tuple(Path(args[-1]).stem for args in compiles),
                             tuple(sorted(LINK_RAM_MODULES)) + ("mac_link_ram_layout",))
            for args in compiles:
                self.assertEqual(Path(args[-1]).parent.name, "mac-link-ram")
                self.assertTrue({"--model-large", "--debug", "--Werror", "-DCC2530_BANKED_JOIN",
                                 "-DCC2530_JOIN_WORKSPACE", "-DCC2530_MAC_LINK",
                                 "-DCC2530_MAC_RECONFIG", "-DCC2530_MAC_ADAPTER"} <= set(args))
                self.assertFalse({"--stack-auto", "--xstack", "-DCC2530_HOST_TEST",
                                  "-DCC2530_BANKED_MAC"} & set(args))
            links = [args for args in commands if args[0] == "sdcc" and "-c" not in args]
            self.assertEqual(len(links), 1)
            self.assertTrue(links[0][-1].endswith("/mac-adapter/mac_adapter_fixture.rel"))
            self.assertNotIn("-DCC2530_MAC_LINK_RAM", links[0])
            native = [args for args in commands if args[0] == "cc"]
            self.assertEqual(len(native), 6)
            self.assertTrue(all("-DCC2530_MAC_LINK_RAM" in args and "-DCC2530_BANKED_JOIN" not in args
                                for args in native))
            self.assertEqual(sum("-fno-sanitize-recover=all" in args for args in native), 3)
            self.assertEqual(sum("tests/test_mac_link_ram.c" in args for args in native), 2)
            self.assertEqual(sum("tests/test_mac_link_projection.c" in args for args in native), 2)
            self.assertTrue(all("tests/test_mac_link_e2e.c" not in args for args in native))
            for args in native:
                if "tests/test_mac_link_ram.c" in args:
                    self.assertEqual({Path(arg).stem for arg in args if arg.startswith("src/")},
                                     LINK_RAM_MODULES)
                if "tests/test_mac_link_projection.c" in args:
                    self.assertIn("tests/mac_attempt_projection.c", args)
                    self.assertNotIn("src/mac_attempt.c", args)
            runs = [Path(args[0]).name for args in commands
                    if args[0] not in ("cc", "sdcc", "mkdir", "cp", "python3")]
            self.assertEqual(runs, ["host-mac-link-ram-tests", "host-mac-link-ram-tests-sanitize",
                                   "host-mac-link-ram-join-tests", "host-mac-link-ram-join-tests-sanitize",
                                   "host-mac-link-projection-tests", "host-mac-link-projection-tests-sanitize"])
            self.assertEqual(sum("tools/link_ram_resources.py" in args for args in commands), 1)
            self.assertFalse(any("tests/boot_" in arg for args in commands for arg in args))
            for unit in ("mac-link", "banked-join-success", "mac-adapter", "bringup"):
                self.assertFalse(any("-DCC2530_MAC_LINK_RAM" in args for args in recipe(board, unit)))

    def test_workspace_keeps_all_modules_and_separates_the_previous_profile(self):
        for board in BOARDS:
            commands = recipe(board, "mac-link-workspace")
            compiles = [args for args in commands if args[0] == "sdcc" and "-c" in args
                        and "-DCC2530_MAC_LINK_WORKSPACE" in args]
            self.assertEqual(tuple(Path(args[-1]).stem for args in compiles),
                             tuple(sorted(LINK_WORKSPACE_MODULES)) + ("mac_link_ram_layout",))
            for args in compiles:
                self.assertEqual(Path(args[-1]).parent.name, "mac-link-workspace")
                self.assertTrue({"--model-large", "--debug", "--Werror", "-DCC2530_BANKED_JOIN",
                                 "-DCC2530_JOIN_WORKSPACE", "-DCC2530_MAC_LINK_RAM",
                                 "-DCC2530_MAC_RECONFIG", "-DCC2530_MAC_ADAPTER"} <= set(args))
                self.assertFalse({"--stack-auto", "--xstack", "-DCC2530_HOST_TEST"} & set(args))
                if Path(args[-1]).stem == "mac_link_workspace":
                    self.assertNotIn("--dataseg", args)
                    self.assertNotIn("--codeseg", args)
            native = [args for args in commands if args[0] == "cc"]
            self.assertEqual(len(native), 4)
            self.assertTrue(all("-DCC2530_MAC_LINK_WORKSPACE" in args and "-Isrc" in args
                                for args in native))
            self.assertEqual(sum("-fno-sanitize-recover=all" in args for args in native), 2)
            for args in native:
                self.assertIn("src/mac_link_workspace.c", args)
                self.assertNotIn("tests/test_mac_link_ram.c", args)
                self.assertNotIn("tests/test_mac_link_e2e.c", args)
                if "tests/test_mac_link_workspace.c" in args:
                    self.assertEqual({Path(arg).stem for arg in args if arg.startswith("src/")},
                                     LINK_WORKSPACE_MODULES)
            links = [args for args in commands if args[0] == "sdcc" and "-c" not in args]
            self.assertEqual(len(links), 1)
            self.assertTrue(links[0][-1].endswith("/mac-adapter/mac_adapter_fixture.rel"))
            self.assertNotIn("-DCC2530_MAC_LINK_WORKSPACE", links[0])
            runs = [Path(args[0]).name for args in commands
                    if args[0] not in ("cc", "sdcc", "mkdir", "cp", "python3")]
            self.assertEqual(runs, ["host-mac-link-workspace-tests", "host-mac-link-workspace-tests-sanitize",
                                   "host-mac-link-workspace-join-tests",
                                   "host-mac-link-workspace-join-tests-sanitize"])
            self.assertEqual(sum("tools/link_ram_resources.py" in args and "--workspace" in args
                                 for args in commands), 1)
            self.assertFalse(any("tests/boot_" in arg for args in commands for arg in args))
            for unit in ("mac-link-ram", "mac-link", "banked-join-success", "mac-adapter", "bringup"):
                self.assertFalse(any("-DCC2530_MAC_LINK_WORKSPACE" in args for args in recipe(board, unit)))

    def test_full_target_is_union_of_split_suites_for_every_board_image(self):
        for board in BOARDS:
            for image in IMAGES:
                with self.subTest(board=board, image=image):
                    full = self.dry_run("test", BOARD=board, IMAGE=image)
                    split = self.dry_run("test-common", "test-tools", "test-board", BOARD=board, IMAGE=image)
                    def normalize(commands):
                        return Counter(tuple("OUTPUT" if i and args[i-1] == "--output" else
                                             Path(arg).name if i and args[i-1] == "--emit-header" else arg
                                             if i or arg == "python3" else Path(arg).name
                                             for i, arg in enumerate(args)) for args in commands)
                    self.assertEqual(normalize(full), normalize(split))
                    self.assertEqual(sum("unittest" in args for args in full), 1)
                    self.assertEqual(sum("tests/boot_image.py" in args for args in full), 1)
                    native = [Path(args[0]).name for args in full if args[0] != "python3"]
                    self.assertEqual(native.count(f"host-tests_{board}"), 1)
                    if image != "bringup":
                        prefix = ("host-fixture-tests" if image == "debug_fixture" else
                                  "host-" + image.removesuffix("_fixture").replace("_", "-") + "-fixture-tests")
                        self.assertEqual(native.count(f"{prefix}_{board}"), 1)
                    if image == "timebase_fixture":
                        self.assertEqual(native.count(f"host-timebase-failure-tests_{board}"), 1)

    def test_local_runs_tools_once_components_per_board_and_all_images(self):
        commands = self.dry_run("test-local")
        self.assertEqual(sum("unittest" in args for args in commands), 1)
        expected_components = {
            "timebase", "clock", "irq", "radio_fifo", "dma", "aes", "prng", "radio_rx", "radio_autoack",
            "radio_queue", "radio_tx", "noise_health", "radio_noise",
            "flash", "flash_exec", "flash_write", "nv_record",
            "mac_frame", "mac_tx", "mac_tx_interval", "mac_time", "mac_epoch", "mac_radio", "mac_stamp", "mac_attempt", "mac_handoff", "mac_adapter",
            "mac_scan", "mac_association", "mac_poll", "mac_join",
            "nwk_beacon", "nwk_candidates", "nwk_parent", "nwk_frame", "aps_frame", "protocol_frame",
            "protocol_budget", "zcl_frame", "zcl_value", "zcl_attributes", "zcl_dispatch", "zcl_basic",
            "zcl_identify", "zcl_temperature", "zdo_node", "zdo_srv", "zigbee_security", "zigbee_mmo",
            "zigbee_key_hash", "security_counter", "security_resident_security", "security_resident_mmo",
            "security_resident_key_hash", "security_resident_counter",
            "banked", "banked_security",
            "banked_join_joined-data-update-loss-restart", "banked_join_missing-network-key",
            "banked_join_retained-radio-fault", "banked_join_retained-flash-fault",
            "banked_join_rx-queues", "banked_join_wrap-quarantine", "banked_join_ack-correlation",
            "banked_join_ack-deadlines", "banked_join_zdo-server", "banked_join_broadcast-table",
            "banked_join_address-map", "banked_join_update-full", "banked_join_install-timeout",
            "banked_join_node-correlation", "banked_join_node-timeout", "banked_join_node-status",
            "banked_join_tc-key-timeout", "banked_join_tc-confirm-timeout", "banked_join_parent-status",
            "banked_join_network-key-late", "banked_join_tc-key-late", "banked_join_tc-confirm-late",
        }
        components = Counter()
        images = Counter()
        outputs = set()
        for args in commands:
            if len(args) < 3 or not args[2].startswith("tests/boot_"):
                continue
            output = Path(args[args.index("--output")+1])
            if args[2] == "tests/boot_image.py":
                board, image = args[args.index("--board")+1], args[args.index("--image")+1]
                self.assertEqual(output.parts[-2:], (board, image))
                images[board, image] += 1
                outputs.add(output)
            else:
                self.assertEqual(output.name, "components")
                component = Path(args[2]).stem.removeprefix("boot_")
                if component == "security_resident":
                    component += "_" + args[args.index("--profile")+1]
                if component == "banked_join":
                    component += "_" + args[args.index("--case")+1]
                components[output.parent.name, component] += 1
        self.assertEqual(images, Counter({(b, i): 1 for b in BOARDS for i in IMAGES}))
        self.assertEqual(components, Counter({(b, c): 1 for b in BOARDS for c in expected_components}))
        self.assertEqual(len(outputs), len(BOARDS)*len(IMAGES))

    def test_ci_component_partition_is_exact_union(self):
        for board in BOARDS:
            def normalized(targets):
                commands = self.dry_run(*targets, BOARD=board)
                return Counter(tuple("OUTPUT" if i and args[i-1] == "--output" else
                                     Path(arg).name if i and args[i-1] == "--emit-header" else arg
                                     if i or arg == "python3" else Path(arg).name
                                     for i, arg in enumerate(args)) for args in commands)
            self.assertEqual(normalized(("test-common",)),
                             normalized(("test-common-core", *(target for _, targets in COMPONENTS.values()
                                                               for target in targets))))
            core = self.dry_run("test-common-core", BOARD=board)
            self.assertFalse(any("tests/boot_mac_radio.py" in args for args in core))
            self.assertFalse(any("tests/boot_mac_stamp.py" in args for args in core))
            self.assertFalse(any("tests/boot_zcl_temperature.py" in args for args in core))
            self.assertFalse(any("tests/boot_mac_attempt.py" in args for args in core))
            self.assertFalse(any("tests/boot_mac_handoff.py" in args for args in core))
            self.assertFalse(any("tests/boot_mac_adapter.py" in args for args in core))
            self.assertFalse(any("tests/boot_mac_join.py" in args for args in core))
            self.assertFalse(any("tests/boot_zdo_node.py" in args for args in core))
            self.assertFalse(any("tests/boot_zdo_srv.py" in args for args in core))
            self.assertFalse(any("tests/boot_zigbee_security.py" in args for args in core))
            self.assertFalse(any("tests/boot_zigbee_mmo.py" in args for args in core))
            self.assertFalse(any("tests/boot_zigbee_key_hash.py" in args for args in core))
            self.assertFalse(any("tests/boot_security_counter.py" in args for args in core))
            self.assertFalse(any("tests/boot_security_resident.py" in args for args in core))

    def test_composed_snapshots_every_listing_after_its_link(self):
        cases = (
            ("radio_noise", (("timebase", "timebase"), ("noise_health", "noise_health"),
                             ("radio_noise", "radio_noise"), ("radio_noise_test", "radio_noise_test"))),
            ("radio_autoack", (("timebase", "timebase"), ("radio_autoack", "radio_autoack"),
                               ("radio_autoack_test", "radio_autoack_test"))),
            ("radio_tx", (("timebase", "timebase"), ("radio_fifo", "radio_fifo"),
                          ("radio_tx", "radio_tx"), ("radio_tx_test", "test_radio_tx"))),
            ("mac_tx", (("mac_frame", "mac_frame"), ("mac_tx", "mac_tx"),
                        ("mac_tx_test", "mac_tx_test"))),
            ("mac_time", (("timebase", "timebase"), ("mac_time", "mac_time"),
                          ("mac_time_test", "test_mac_time"))),
            ("mac_epoch", (("mac_epoch", "mac_epoch"), ("mac_epoch_test", "mac_epoch_test"))),
            ("mac_stamp", (("mac_epoch", "mac_epoch"), ("mac_stamp", "mac_stamp"),
                           ("mac_stamp_test", "mac_stamp_test"))),
            ("zdo_node", (("zdo_node", "zdo_node"), ("aps_frame", "aps_frame"),
                          ("zdo_node_test", "zdo_node_test"))),
            ("zdo_srv", tuple((m, m) for m in
                             ("zdo_srv", "zdo_node", "aps_frame", "nwk_frame", "zdo_srv_test"))),
            ("security_counter", tuple((m, m) for m in
                                       ("flash_exec", "flash", "flash_write", "nv_record",
                                        "security_counter", "security_counter_test"))),
            ("mac_radio", (("mr_timebase", "timebase"), ("mr_clock", "clock"),
                           ("mr_mac_time", "mac_time"), ("mr_radio_autoack", "radio_autoack"),
                           ("mr_mac_epoch", "mac_epoch"), ("mr_mac_radio", "mac_radio"),
                           ("mac_radio_test", "test_mac_radio"))),
            ("mac_attempt", tuple(("ma_" + module, module) for module in (
                "timebase", "clock", "mac_time", "radio_autoack", "mac_epoch", "mac_radio",
                "mac_attempt", "test_mac_attempt"))),
            ("mac_handoff", tuple(("mh_" + module, module) for module in (
                "timebase", "clock", "mac_time", "radio_autoack", "mac_epoch", "mac_radio",
                "mac_attempt", "test_mac_handoff"))),
            ("mac_scan", (("mac_frame", "mac_frame"), ("mac_tx", "mac_tx"),
                          ("nwk_beacon", "nwk_beacon"), ("nwk_candidates", "nwk_candidates"),
                          ("mac_scan", "mac_scan"), ("mac_scan_test", "mac_scan_test"))),
            ("mac_association", (("mac_frame", "mac_frame"), ("mac_association", "mac_association"),
                                 ("mac_association_test", "mac_association_test"))),
            ("mac_poll", (("mac_frame", "mac_frame"), ("mac_tx", "mac_tx"),
                          ("mac_association", "mac_association"), ("mac_poll", "mac_poll"),
                          ("mac_poll_test", "mac_poll_test"))),
            ("nwk_candidates", (("mac_frame", "mac_frame"), ("nwk_beacon", "nwk_beacon"),
                                ("nwk_candidates", "nwk_candidates"),
                                ("nwk_candidates_test", "nwk_candidates_test"))),
            ("nwk_parent", (("mac_frame", "mac_frame"), ("nwk_beacon", "nwk_beacon"),
                            ("nwk_candidates", "nwk_candidates"), ("nwk_parent", "nwk_parent"),
                            ("nwk_parent_test", "nwk_parent_test"))),
            ("zcl_basic", (("zcl_basic", "zcl_basic"), ("zcl_dispatch", "zcl_dispatch"),
                           ("zcl_write", "zcl_write"),
                           ("zcl_attributes", "zcl_attributes"), ("zcl_frame", "zcl_frame"),
                           ("zcl_value", "zcl_value"), ("zcl_basic_test", "zcl_basic_test"))),
            ("zcl_identify", (("zcl_identify", "zcl_identify"), ("zcl_dispatch", "zcl_dispatch"),
                              ("zcl_write", "zcl_write"),
                              ("zcl_attributes", "zcl_attributes"), ("zcl_frame", "zcl_frame"),
                              ("zcl_value", "zcl_value"), ("zcl_identify_test", "zcl_identify_test"))),
            ("zcl_dispatch", (("zcl_dispatch", "zcl_dispatch"), ("zcl_write", "zcl_write"),
                              ("zcl_attributes", "zcl_attributes"), ("zcl_frame", "zcl_frame"),
                              ("zcl_value", "zcl_value"), ("zcl_dispatch_test", "zcl_dispatch_test"))),
            ("protocol_budget", tuple((m, m) for m in (
                "mac_frame", "nwk_frame", "aps_frame", "zcl_frame", "zcl_value",
                "zcl_attributes", "zcl_dispatch", "zcl_write", "protocol_budget_test"))),
        )
        for board, service, modules in ((b, s, m) for b in BOARDS for s, m in cases):
            commands = self.dry_run("test-" + service.replace("_", "-"), include_build=True, BOARD=board)
            link = next(args for args in commands if args[0] == "sdcc" and "-c" not in args)
            self.assertEqual([Path(arg).name for arg in link if arg.endswith(".rel")],
                             [f"{source}.rel" for source, _ in modules])
            simulation = next(args for args in commands if f"tests/boot_{service}.py" in args)
            output = Path(simulation[simulation.index("--output")+1])
            snapshots = [args for args in commands if args[0] == "cp"]
            self.assertEqual([(Path(args[1]).name, Path(args[2]).name) for args in snapshots],
                             [(f"{source}.rst", f"{service}_test.{module}.rst")
                              for source, module in modules])
            start = commands.index(link) + 1
            self.assertEqual(commands[start:start+len(snapshots)], snapshots)
            for args in snapshots:
                self.assertEqual(Path(args[1]).parent, output)
                self.assertEqual(Path(args[2]).parent, output)
                self.assertLess(commands.index(link), commands.index(args))
                self.assertLess(commands.index(args), commands.index(simulation))
            native = [args for args in commands
                      if Path(args[0]).name == "host-" + service.replace("_", "-") + "-tests"]
            self.assertEqual(len(native), 1)
            self.assertEqual(Path(native[0][0]).parent, output)
            self.assertLess(commands.index(native[0]), commands.index(simulation))

    def test_partitioned_compositions_and_immediate_snapshots(self):
        cases = (
            ("zcl_temperature", ("zcl_temperature", "zcl_dispatch", "zcl_write", "zcl_attributes",
                                 "zcl_frame", "zcl_value"), ("wire", "config", "report"), "ZCL_TEMP_PART", 1),
            ("mac_join", ("mac_frame", "mac_tx", "mac_association", "mac_poll", "mac_join"),
             tuple(map(str, range(22))), "MAC_JOIN_CASE", 0),
        )
        for board, (service, production, parts, define, first) in ((b, c) for b in BOARDS for c in cases):
            commands = self.dry_run("test-" + service.replace("_", "-"), include_build=True, BOARD=board)
            links = [args for args in commands if args[0] == "sdcc" and "-c" not in args]
            self.assertEqual(len(links), len(parts))
            proofs = [args for args in commands if f"tests/boot_{service}.py" in args]
            self.assertEqual(len(proofs), 1)
            output = Path(proofs[0][proofs[0].index("--output")+1])
            snapshots = []
            for number, (part, link) in enumerate(zip(parts, links), first):
                stem = f"{service}_{part}_test"
                modules = production + (stem,)
                self.assertEqual(link[link.index("-o")+1], str(output / f"{stem}.ihx"))
                self.assertEqual([Path(arg).name for arg in link if arg.endswith(".rel")],
                                 [f"{module}.rel" for module in modules])
                compile_args = [args for args in commands if args[0] == "sdcc"
                                and "-c" in args and args[-1] == str(output / f"{stem}.rel")]
                self.assertEqual(len(compile_args), 1)
                self.assertIn(f"-D{define}={number}", compile_args[0])
                self.assertIn(f"tests/test_{service}.c", compile_args[0])
                expected = [["cp", str(output / f"{module}.rst"),
                             str(output / f"{stem}.{module}.rst")] for module in modules]
                start = commands.index(link) + 1
                self.assertEqual(commands[start:start+len(modules)], expected)
                self.assertLess(start+len(modules)-1, commands.index(proofs[0]))
                snapshots.extend(expected)
            self.assertEqual([args for args in commands if args[0] == "cp"], snapshots)
            for args in commands:
                if args[0] == "cc":
                    self.assertFalse(any(arg.startswith("-D" + define) for arg in args))
                    self.assertEqual([arg for arg in args if arg.startswith("src/")],
                                     [f"src/{module}.c" for module in production])

    def test_bounded_sanitizers_and_no_board_linkage(self):
        for board, service in ((b, s) for b in BOARDS for s in
                               ("zcl-basic", "zcl-identify", "zcl-temperature", "radio-autoack",
                                "mac-epoch", "mac-radio", "mac-stamp", "mac-attempt", "mac-handoff", "mac-adapter", "mac-join",
                                "zdo-node", "zdo-srv", "zigbee-security", "zigbee-mmo", "zigbee-key-hash",
                                "security-counter")):
            commands = self.dry_run("test-" + service, include_build=True, BOARD=board)
            sanitize = next(args for args in commands
                            if args[0] == "cc" and "-fsanitize=address,undefined" in args
                            and Path(args[-1]).name == "host-" + service + "-tests-sanitize")
            self.assertIn("-fno-sanitize-recover=all", sanitize)
            self.assertEqual(Path(sanitize[-1]).name, "host-" + service + "-tests-sanitize")
            native = [Path(args[0]).name for args in commands]
            self.assertEqual(native.count("host-" + service + "-tests"), 1)
            self.assertEqual(native.count("host-" + service + "-tests-sanitize"), 1)
            for image in IMAGES:
                commands = self.dry_run("all", include_build=True, BOARD=board, IMAGE=image)
                linked = any(service.replace("-", "_") in arg for args in commands for arg in args)
                self.assertEqual(linked, service == "radio-autoack" and image == "radio_link_fixture")

    def test_crypto_compositions_snapshot_real_aes_and_every_object(self):
        cases = (
            ("zigbee-security", "security_test",
             ("timebase", "aes", "ccm_star", "nwk_frame", "aps_frame", "zigbee_security", "security_test"),
             ("src/ccm_star.c", "src/zigbee_security.c")),
            ("zigbee-mmo", "mmo_test", ("timebase", "aes", "zigbee_mmo", "mmo_test"),
             ("src/zigbee_mmo.c",)),
            ("zigbee-key-hash", "key_hash_test",
             ("timebase", "aes", "zigbee_mmo", "zigbee_key_hash", "key_hash_test"),
             ("src/zigbee_mmo.c", "src/zigbee_key_hash.c")),
        )
        for board, (service, stem, modules, sources) in ((b, c) for b in BOARDS for c in cases):
            commands = self.dry_run("test-" + service, include_build=True, BOARD=board)
            link = next(args for args in commands if args[0] == "sdcc" and "-c" not in args)
            self.assertEqual([Path(p).name for p in link if p.endswith(".rel")],
                             [m + ".rel" for m in modules])
            proof = next(args for args in commands if f"tests/boot_{service.replace('-', '_')}.py" in args)
            output = Path(proof[proof.index("--output")+1])
            snapshots = [["cp", str(output / f"{m}.rst"), str(output / f"{stem}.{m}.rst")]
                         for m in modules]
            self.assertEqual(commands[commands.index(link)+1:commands.index(link)+1+len(modules)], snapshots)
            self.assertEqual([args for args in commands if args[0] == "cp"], snapshots)
            self.assertLess(commands.index(link)+len(modules), commands.index(proof))
            for args in commands:
                if args[0] == "cc":
                    for source in ("src/aes.c", "tests/host_mmio.c", "tests/security_aes_model.c",
                                   "tests/aes_reference.c", *sources):
                        self.assertIn(source, args)
                if args[0] == "sdcc":
                    self.assertNotIn("tests/aes_reference.c", args)
                    self.assertNotIn("tests/security_aes_model.c", args)

    def test_resident_profile_has_physical_reservations_and_immediate_snapshots(self):
        modules = ("security_iram_low", "security_iram_high", "flash_exec", "flash", "flash_write",
                   "nv_record", "security_counter", "timebase", "aes", "ccm_star", "nwk_frame",
                   "aps_frame", "zigbee_security", "zigbee_mmo", "zigbee_key_hash")
        callers = ("security_test", "mmo_test", "key_hash_test", "security_counter_test")
        for board in BOARDS:
            commands = self.dry_run("test-security-resident", "test-zigbee-key-hash",
                                    include_build=True, BOARD=board)
            proof = next(args for args in commands if "tests/boot_security_resident.py" in args)
            output = Path(proof[proof.index("--output")+1])
            links = [args for args in commands if args[0] == "sdcc" and "-c" not in args]
            self.assertEqual(len(links), 5)
            for link, caller in zip(links, callers):
                self.assertEqual([p for p in link if p.endswith(".rel")],
                                 [str(output / "resident" / (m+".rel")) for m in modules+(caller,)])
                stem = Path(link[link.index("-o")+1]).stem
                profile = stem.removeprefix("resident_")
                proof = next(args for args in commands if "tests/boot_security_resident.py" in args
                             and args[args.index("--profile")+1] == profile)
                snapshots = [["cp", str(output / "resident" / (m+".rst")),
                              str(output / f"{stem}.{m}.rst")] for m in modules+(caller,)]
                self.assertEqual(commands[commands.index(link)+1:commands.index(link)+17], snapshots)
                self.assertLess(commands.index(link)+16, commands.index(proof))
            split = self.dry_run("test-security-resident-security", "test-security-resident-mmo",
                                 "test-security-resident-key-hash", "test-security-resident-counter",
                                 BOARD=board)
            self.assertEqual([args[args.index("--profile")+1] for args in split],
                             ["security", "mmo", "key_hash", "counter"])
            for args in commands:
                if args[0] == "sdcc" and "-c" in args:
                    selected = Path(args[-1]).parent == output / "resident"
                    self.assertEqual("-DCC2530_SECURITY_RESIDENT" in args, selected)
                    reserved = Path(args[-1]).stem in modules[:2]
                    self.assertEqual("--dataseg" in args, selected and not reserved)
            for image in IMAGES:
                commands = self.dry_run("all", include_build=True, BOARD=board, IMAGE=image)
                self.assertFalse(any("CC2530_SECURITY_RESIDENT" in arg or "security_iram" in arg
                                     or "/resident/" in arg for args in commands for arg in args))

    def test_ed_integration_keeps_real_services_and_host_only_models(self):
        new_modules = {"ed_wire", "security_keys", "nwk_aps", "nwk_aps_transmit",
                       "zdo_runtime", "bdb_join", "bdb_join_init"}
        target_modules = new_modules | {"bdb_join_layout"}
        for board in BOARDS:
            commands = self.dry_run("test-ed-integration", include_build=True, BOARD=board)
            builds = [args for args in commands if args[0] == "cc"]
            self.assertEqual(len(builds), 6)
            for args in builds:
                for source in ("src/aes.c", "src/ccm_star.c", "src/ed_wire.c",
                               "tests/security_aes_model.c", "tests/aes_reference.c"):
                    self.assertIn(source, args)
                self.assertNotIn("-DNDEBUG", args)
                if "tests/test_ed_wire.c" not in args:
                    for source in ("src/security_keys.c", "src/security_counter.c",
                                   "src/nv_record.c", "src/flash_write.c", "src/flash_exec.c",
                                   "tests/security_joint_model.c", "tests/host_flash_engine.c"):
                        self.assertIn(source, args)
                if "tests/test_bdb_join.c" in args:
                    for module in ("mac_tx", "mac_scan", "mac_poll", "mac_association", "mac_join",
                                   "nwk_candidates", "nwk_parent", "nwk_aps", "zdo_runtime", "bdb_join"):
                        self.assertIn(f"src/{module}.c", args)
            sanitized = [args for args in builds if "-fsanitize=address,undefined" in args]
            self.assertEqual(len(sanitized), 3)
            for args in sanitized:
                self.assertIn("-fno-sanitize-recover=all", args)
            target = [args for args in commands if args[0] == "sdcc"]
            self.assertEqual({Path(args[args.index("-c")+1]).stem for args in target}, target_modules)
            for args in target:
                self.assertIn("-c", args)
                self.assertEqual([arg for arg in args if arg.startswith("tests/")],
                                 ["tests/bdb_join_layout.c"] if
                                 args[args.index("-c")+1] == "tests/bdb_join_layout.c" else [])
                self.assertNotIn("-DCC2530_HOST_TEST", args)
            for image in IMAGES:
                commands = self.dry_run("all", include_build=True, BOARD=board, IMAGE=image)
                self.assertFalse(any(Path(arg).stem in target_modules
                                     for args in commands for arg in args if arg.endswith((".c", ".rel"))))

    def test_banked_profile_is_isolated_and_uses_reviewed_abi_and_immediate_snapshots(self):
        modules = ("flash_exec", "banked", "banked_fixture", "banked_fixture_bank1",
                   "banked_fixture_bank2", "banked_fixture_bank7")
        for board in BOARDS:
            commands = self.dry_run("test-banked", include_build=True, BOARD=board)
            compiles = [a for a in commands if a[0] == "sdcc" and "-c" in a]
            self.assertEqual([Path(a[-1]).stem for a in compiles], list(modules))
            for args, module in zip(compiles, modules):
                self.assertIn("--model-large", args)
                self.assertIn("--debug", args)
                self.assertIn("--Werror", args)
                self.assertFalse(set(args) & {"--model-huge", "--stack-auto", "--xstack",
                                             "--parms-in-bank1", "--stack-loc"})
                if module[-1] in "127":
                    self.assertEqual(args[args.index("--codeseg")+1], "BANK"+module[-1])
                    self.assertEqual(args[args.index("--constseg")+1], "BANK"+module[-1]+"_CONST")
                else:
                    self.assertNotIn("--codeseg", args)
            link = next(a for a in commands if a[0] == "sdcc" and "-c" not in a)
            self.assertEqual([Path(a).stem for a in link if a.endswith(".rel")], list(modules))
            self.assertEqual(link[link.index("--code-size")+1], "0x80000")
            self.assertEqual(link[link.index("--stack-size")+1], "0x5b")
            self.assertEqual(link[link.index("--xram-size")+1], "0x1e00")
            for flag in ("-Wl-r", "-Wl-bBANK1=0x18000", "-Wl-bBANK2=0x28000",
                         "-Wl-bBANK7=0x78000", "-Wl-bBANK7_CONST=0x7e7f8"):
                self.assertIn(flag, link)
            directory = Path(link[link.index("-o")+1]).parent
            index = commands.index(link)+1
            self.assertEqual(commands[index:index+6],
                             [["cp", str(directory/f"{m}.rst"), str(directory/f"banked.{m}.rst")]
                              for m in modules])
            self.assertIn("tools/banked_image.py", commands[index+6])
            self.assertIn("tests/boot_banked.py", commands[index+7])
            for image in IMAGES:
                old = self.dry_run("all", include_build=True, BOARD=board, IMAGE=image)
                self.assertFalse(any("/banked/" in a or a.endswith("/banked.c") for cmd in old for a in cmd))
                old_link = next(a for a in old if a[0] == "sdcc" and "-c" not in a)
                self.assertEqual(old_link[old_link.index("--code-size")+1], "0x8000")

    def test_mac_radio_profile_does_not_leak_to_legacy_objects(self):
        for board in BOARDS:
            commands = self.dry_run("test-mac-radio", "test-mac-time", "test-radio-autoack",
                                    include_build=True, BOARD=board)
            profiled = []
            for args in commands:
                if args[0] != "sdcc" or "-c" not in args:
                    continue
                name = Path(args[-1]).name
                selected = name.startswith("mr_") or name == "mac_radio_test.rel"
                self.assertEqual("-DCC2530_MAC_RADIO" in args, selected)
                if selected: profiled.append(name)
            self.assertEqual(len(profiled), 7)

    def test_banked_security_reservations_snapshots_and_isolated_far_abi(self):
        modules = ("banked_security_iram_low", "banked_security_iram_high", "flash_exec", "flash",
                   "flash_write", "nv_record", "security_counter", "timebase", "aes", "ccm_star",
                   "zigbee_mmo", "zigbee_key_hash", "nwk_frame", "aps_frame", "banked", "ed_wire",
                   "security_keys", "banked_security_fixture")
        for board in BOARDS:
            commands = self.dry_run("test-banked-security", "test-ed-integration", include_build=True, BOARD=board)
            link = next(a for a in commands if a[0] == "sdcc" and "-c" not in a)
            self.assertEqual([Path(a).stem for a in link if a.endswith(".rel")], list(modules))
            self.assertEqual(link[link.index("--code-size")+1], "0x80000")
            self.assertEqual(link[link.index("--stack-size")+1], "0x2b")
            directory = Path(link[link.index("-o")+1]).parent
            start = commands.index(link)+1
            self.assertEqual(commands[start:start+len(modules)],
                             [["cp", str(directory/f"{m}.rst"), str(directory/f"banked_security.{m}.rst")]
                              for m in modules])
            packer = commands[start+len(modules)]
            self.assertIn("tools/banked_image.py", packer)
            self.assertEqual(packer[-2:], ["--profile", "security"])
            for args in commands:
                if args[0] != "sdcc" or "-c" not in args:
                    continue
                selected = Path(args[-1]).parent == directory
                self.assertEqual("-DCC2530_BANKED_SECURITY" in args, selected)
                self.assertEqual("-DBANKED_STACK_FIRST=0x52" in args, selected)
                self.assertFalse(set(args) & {"--model-huge", "--stack-auto", "--xstack", "--parms-in-bank1"})
                module = Path(args[-1]).stem
                self.assertEqual("--codeseg" in args, selected and module in ("ed_wire", "security_keys"))
            builds = [a for a in commands if a[0] == "cc" and "tests/banked_security_vectors.c" in a]
            self.assertEqual(len(builds), 2)
            self.assertEqual(sum("-fno-sanitize-recover=all" in a for a in builds), 1)
            for args in builds:
                self.assertNotIn("-DCC2530_BANKED_SECURITY", args)
                self.assertNotIn("-DNDEBUG", args)

    def test_complete_join_profile_keeps_every_service_alias_reservation_and_snapshot(self):
        modules = ("banked_join_iram_low", "banked_join_iram_high", "flash_exec", "flash",
                   "flash_write", "nv_record", "security_counter", "timebase", "aes", "ccm_star",
                   "zigbee_mmo", "zigbee_key_hash", "mac_frame", "banked", "nwk_frame", "aps_frame",
                   "ed_wire", "security_keys", "mac_tx", "mac_poll", "mac_association", "mac_join",
                   "zdo_node", "zdo_srv", "nwk_beacon", "nwk_candidates", "nwk_parent", "mac_scan",
                   "bdb_join", "bdb_join_init", "nwk_aps", "nwk_aps_transmit", "zdo_runtime",
                   "banked_join_fixture")
        for board in BOARDS:
            commands = recipe(board, "banked-join-success")
            compiles = [args for args in commands if args[0] == "sdcc" and "-c" in args]
            self.assertEqual(tuple(Path(args[-1]).stem for args in compiles), modules)
            for args in compiles:
                self.assertTrue({"--model-large", "--debug", "--Werror", "-DCC2530_BANKED_JOIN",
                                 "-DCC2530_JOIN_WORKSPACE", "-DBANKED_STACK_FIRST=0x50"} <= set(args))
                self.assertFalse({"--model-huge", "--stack-auto", "--xstack",
                                  "--parms-in-bank1", "-DCC2530_HOST_TEST"} & set(args))
            link = next(args for args in commands if args[0] == "sdcc" and "-c" not in args)
            self.assertEqual(tuple(Path(arg).stem for arg in link if arg.endswith(".rel")), modules)
            for option, expected in (("--stack-size", "0x2d"), ("--xram-size", "0x1e00"),
                                     ("--code-size", "0x80000")):
                self.assertEqual(link[link.index(option)+1], expected)
            for bank in range(1, 5):
                self.assertIn(f"-Wl-bBJ_BANK{bank}=0x{bank}8000", link)
            directory = Path(link[link.index("-o")+1]).parent
            copies = commands[commands.index(link)+1]
            self.assertEqual(copies, tuple(arg for module in modules for arg in
                             ("cp", str(directory/f"{module}.rst"), str(directory/f"banked_join.{module}.rst")+";")))
            generated = [args for args in commands if "tests/verify_banked_join.py" in args]
            self.assertEqual(len(generated), 1)
            self.assertIn("--emit-header", generated[0])
            native = [args for args in commands if args[0] == "cc"]
            self.assertEqual(len(native), 2)
            self.assertEqual(sum("-fno-sanitize-recover=all" in args for args in native), 1)
            self.assertTrue(all("-DCC2530_BANKED_JOIN" not in args and "-DNDEBUG" not in args for args in native))
            for args in recipe(board, "bringup"):
                self.assertFalse(any("banked-join" in arg or "-DCC2530_BANKED_JOIN" == arg for arg in args))

    def test_attempt_profile_is_separate_from_radio_and_legacy_objects(self):
        for board in BOARDS:
            commands = self.dry_run("test-mac-attempt", "test-mac-radio",
                                    "test-mac-time", "test-radio-autoack",
                                    include_build=True, BOARD=board)
            profiled = []
            for args in commands:
                if args[0] == "sdcc" and "-c" in args:
                    name = Path(args[-1]).name
                    attempt = name.startswith("ma_")
                    radio = name.startswith("mr_") or name == "mac_radio_test.rel"
                    self.assertEqual("-DCC2530_MAC_ATTEMPT" in args, attempt)
                    self.assertEqual("-DCC2530_MAC_RADIO" in args, attempt or radio)
                    if attempt:
                        profiled.append(name)
                elif args[0] == "cc":
                    attempt = Path(args[-1]).name.startswith("host-mac-attempt-tests")
                    self.assertEqual("-DCC2530_MAC_ATTEMPT" in args, attempt)
                    if attempt:
                        self.assertIn("-DCC2530_MAC_RADIO", args)
            self.assertEqual(len(profiled), 8)

    def test_noise_health_standalone_and_only_explicit_noise_board_linkage(self):
        for board in BOARDS:
            commands = self.dry_run("test-noise-health", include_build=True, BOARD=board)
            links = [args for args in commands if args[0] == "sdcc" and "-c" not in args]
            self.assertEqual(len(links), 1)
            self.assertEqual([Path(arg).name for arg in links[0] if arg.endswith(".rel")],
                             ["noise_health.rel", "noise_health_test.rel"])
            self.assertEqual(sum("tests/boot_noise_health.py" in args for args in commands), 1)
            self.assertEqual(sum(Path(args[0]).name == "host-noise-health-tests"
                                 for args in commands), 1)
            for image in IMAGES:
                commands = self.dry_run("all", include_build=True, BOARD=board, IMAGE=image)
                linked = any("noise_health" in arg or "radio_noise" in arg for args in commands for arg in args)
                self.assertEqual(linked, image == "radio_noise_fixture")
                self.assertFalse(any("test_radio_noise.c" in arg or "test_noise_health.c" in arg
                                     for args in commands for arg in args))

    def test_clock_and_tx_snapshots_are_immediate_and_image_specific(self):
        for board in BOARDS:
            cases = (
                ("test-clock", "bringup", "clock_test",
                 ("timebase", "clock", "clock_test"), (("clock", "clock"),)),
                ("test-board", "clock_fixture", "clock_fixture",
                 (f"startup_{board}", f"status_{board}", f"board_{board}",
                  f"example_clock_fixture_{board}", "clock_fixture_state", "timebase", "clock"),
                 (("clock", "clock"),)),
                ("test-board", "radio_tx_fixture", "radio_tx_fixture",
                 ("timebase", "radio_fifo", "radio_tx", "clock", f"startup_{board}",
                  f"status_{board}", f"board_{board}", f"example_radio_tx_fixture_{board}",
                  "radio_tx_fixture_state"),
                 (("timebase", "timebase"), ("radio_fifo", "radio_fifo"), ("radio_tx", "radio_tx"),
                  ("clock", "clock"), (f"startup_{board}", "startup"), (f"status_{board}", "status"),
                  (f"board_{board}", board), (f"example_radio_tx_fixture_{board}", "radio_tx_fixture"),
                  ("radio_tx_fixture_state", "radio_tx_fixture_state"))),
                ("test-board", "radio_noise_fixture", "radio_noise_fixture",
                 ("timebase", "clock", "noise_health", "radio_noise", f"startup_{board}",
                  f"status_{board}", f"board_{board}", f"example_radio_noise_fixture_{board}",
                  "radio_noise_fixture_state"),
                 (("timebase", "timebase"), ("clock", "clock"), ("noise_health", "noise_health"),
                  ("radio_noise", "radio_noise"), (f"startup_{board}", "startup"), (f"status_{board}", "status"),
                  (f"board_{board}", board), (f"example_radio_noise_fixture_{board}", "radio_noise_fixture"),
                  ("radio_noise_fixture_state", "radio_noise_fixture_state"))),
                ("test-board", "radio_link_fixture", "radio_link_fixture",
                 ("timebase", "clock", "radio_autoack", f"startup_{board}",
                  f"status_{board}", f"board_{board}", f"example_radio_link_fixture_{board}",
                  "radio_link_fixture_state"),
                 (("timebase", "timebase"), ("clock", "clock"), ("radio_autoack", "radio_autoack"),
                  (f"startup_{board}", "startup"), (f"status_{board}", "status"),
                  (f"board_{board}", board), (f"example_radio_link_fixture_{board}", "radio_link_fixture"),
                  ("radio_link_fixture_state", "radio_link_fixture_state"))),
            )
            for service in ("radio_fifo", "dma", "aes", "prng", "radio_rx"):
                image = service + "_fixture"
                caller = (f"startup_{board}", f"status_{board}", f"board_{board}",
                          f"example_{image}_{board}")
                modules = ((image + "_state", "timebase", "clock", service)
                           if service == "radio_fifo" else
                           ("timebase", "clock", service, image + "_state"))
                cases += (("test-board", image, image, caller + modules,
                           (("clock", "clock"), (service, service))),)
            for target, image, stem, modules, copies in cases:
                with self.subTest(board=board, image=image, target=target):
                    commands = self.dry_run(target, BOARD=board, IMAGE=image, include_build=True)
                    links = [args for args in commands if args[0] == "sdcc" and "-c" not in args]
                    self.assertEqual(len(links), 1)
                    link = links[0]
                    self.assertEqual([Path(arg).name for arg in link if arg.endswith(".rel")],
                                     [module + ".rel" for module in modules])
                    snapshots = [args for args in commands if args[0] == "cp"]
                    self.assertEqual([(Path(args[1]).name, Path(args[2]).name) for args in snapshots],
                                     [(source + ".rst", f"{stem}.{name}.rst") for source, name in copies])
                    start = commands.index(link) + 1
                    self.assertEqual(commands[start:start+len(copies)], snapshots)
                    self.assertTrue(all(Path(args[1]).parent == Path(args[2]).parent for args in snapshots))

    def test_aes_board_builds_its_own_runtime_reference_without_components(self):
        for board in BOARDS:
            commands = self.dry_run("test-board", include_build=True, BOARD=board, IMAGE="aes_fixture")
            reference = [args for args in commands if "-DAES_REFERENCE_MAIN" in args]
            self.assertEqual(len(reference), 1)
            self.assertEqual(Path(reference[0][-1]).name, "aes-reference")
            simulation = next(args for args in commands if "tests/boot_image.py" in args)
            self.assertEqual(Path(reference[0][-1]).parent, Path(simulation[simulation.index("--output")+1]))
            self.assertLess(commands.index(reference[0]), commands.index(simulation))
            self.assertFalse(any("tests/boot_aes.py" in args or "tests/test_aes.c" in args for args in commands))

    def test_link_board_has_nonrecovering_sanitizer_and_no_hardware_operator(self):
        for board in BOARDS:
            commands = self.dry_run("test-board", include_build=True, BOARD=board, IMAGE="radio_link_fixture")
            sanitizer = next(args for args in commands if "-fsanitize=address,undefined" in args)
            self.assertIn("-fno-sanitize-recover=all", sanitizer)
            self.assertEqual(sum(Path(args[0]).name == f"host-radio-link-fixture-sanitize_{board}"
                                 for args in commands), 1)
            self.assertFalse(any("check_radio_link_hardware" in arg or "cc-tool" in arg
                                 for args in commands for arg in args))

    def test_local_stops_at_first_failure_without_later_suites(self):
        with tempfile.TemporaryDirectory(prefix="cc2530-make-failure-") as directory:
            script = Path(directory) / "fake_make.py"
            log = Path(directory) / "calls"
            script.write_text(
                "import os,sys\n"
                "from pathlib import Path\n"
                f"log=Path({str(log)!r})\n"
                "calls=log.read_text() if log.exists() else ''\n"
                "log.write_text(calls + ' '.join(sys.argv[1:]) + '\\n')\n"
                "sys.exit(7 if calls.count('\\n')+1 == int(os.environ['FAIL_AT']) else 0)\n",
                encoding="ascii",
            )
            for boundary in (1, 2, 6, 14):
                log.unlink(missing_ok=True)
                environment = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL")}
                environment["FAIL_AT"] = str(boundary)
                result = subprocess.run(
                    ["make", "--no-print-directory", "-j1", "test-local",
                     f"MAKE={shlex.quote(sys.executable)} {shlex.quote(str(script))}"],
                    cwd=ROOT, env=environment, capture_output=True, text=True, timeout=30,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Error 7", result.stderr)
                self.assertEqual(len(log.read_text().splitlines()), boundary)
