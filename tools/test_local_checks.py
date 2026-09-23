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

    def test_full_target_is_union_of_split_suites_for_every_board_image(self):
        for board in BOARDS:
            for image in IMAGES:
                with self.subTest(board=board, image=image):
                    full = self.dry_run("test", BOARD=board, IMAGE=image)
                    split = self.dry_run("test-common", "test-tools", "test-board", BOARD=board, IMAGE=image)
                    def normalize(commands):
                        return Counter(tuple("OUTPUT" if i and args[i-1] == "--output" else arg
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
            "mac_frame", "mac_tx", "mac_time", "mac_epoch", "mac_radio", "mac_stamp", "mac_attempt",
            "mac_scan", "mac_association", "mac_poll", "mac_join",
            "nwk_beacon", "nwk_candidates", "nwk_parent", "nwk_frame", "aps_frame", "protocol_frame",
            "protocol_budget", "zcl_frame", "zcl_value", "zcl_attributes", "zcl_dispatch", "zcl_basic",
            "zcl_identify", "zcl_temperature", "zdo_node",
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
                components[output.parent.name, Path(args[2]).stem.removeprefix("boot_")] += 1
        self.assertEqual(images, Counter({(b, i): 1 for b in BOARDS for i in IMAGES}))
        self.assertEqual(components, Counter({(b, c): 1 for b in BOARDS for c in expected_components}))
        self.assertEqual(len(outputs), len(BOARDS)*len(IMAGES))

    def test_ci_component_partition_is_exact_union(self):
        for board in BOARDS:
            def normalized(targets):
                commands = self.dry_run(*targets, BOARD=board)
                return Counter(tuple("OUTPUT" if i and args[i-1] == "--output" else arg
                                     if i or arg == "python3" else Path(arg).name
                                     for i, arg in enumerate(args)) for args in commands)
            self.assertEqual(normalized(("test-common",)),
                             normalized(("test-common-core", "test-mac-radio", "test-mac-stamp",
                                         "test-zcl-temperature", "test-mac-attempt", "test-mac-join", "test-zdo-node")))
            core = self.dry_run("test-common-core", BOARD=board)
            self.assertFalse(any("tests/boot_mac_radio.py" in args for args in core))
            self.assertFalse(any("tests/boot_mac_stamp.py" in args for args in core))
            self.assertFalse(any("tests/boot_zcl_temperature.py" in args for args in core))
            self.assertFalse(any("tests/boot_mac_attempt.py" in args for args in core))
            self.assertFalse(any("tests/boot_mac_join.py" in args for args in core))
            self.assertFalse(any("tests/boot_zdo_node.py" in args for args in core))

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
            ("mac_radio", (("mr_timebase", "timebase"), ("mr_clock", "clock"),
                           ("mr_mac_time", "mac_time"), ("mr_radio_autoack", "radio_autoack"),
                           ("mr_mac_epoch", "mac_epoch"), ("mr_mac_radio", "mac_radio"),
                           ("mac_radio_test", "test_mac_radio"))),
            ("mac_attempt", tuple(("ma_" + module, module) for module in (
                "timebase", "clock", "mac_time", "radio_autoack", "mac_epoch", "mac_radio",
                "mac_attempt", "test_mac_attempt"))),
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
                                "mac-epoch", "mac-radio", "mac-stamp", "mac-attempt", "mac-join", "zdo-node")):
            commands = self.dry_run("test-" + service, include_build=True, BOARD=board)
            sanitize = next(args for args in commands
                            if args[0] == "cc" and "-fsanitize=address,undefined" in args)
            self.assertIn("-fno-sanitize-recover=all", sanitize)
            self.assertEqual(Path(sanitize[-1]).name, "host-" + service + "-tests-sanitize")
            native = [Path(args[0]).name for args in commands]
            self.assertEqual(native.count("host-" + service + "-tests"), 1)
            self.assertEqual(native.count("host-" + service + "-tests-sanitize"), 1)
            for image in IMAGES:
                commands = self.dry_run("all", include_build=True, BOARD=board, IMAGE=image)
                linked = any(service.replace("-", "_") in arg for args in commands for arg in args)
                self.assertEqual(linked, service == "radio-autoack" and image == "radio_link_fixture")

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
