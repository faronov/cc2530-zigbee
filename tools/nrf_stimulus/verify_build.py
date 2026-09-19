# SPDX-License-Identifier: BSD-3-Clause
"""Execute a static audit of a trusted, locally built pinned workspace.

Not a sandbox or a certifier for arbitrary downloaded build metadata. Git,
Python, host libraries, SDK headers/build scripts and the non-concurrently
modified workspace remain trusted. No target firmware is executed.
"""

import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

from tools.nrf_stimulus import artifact, protocol

HERE = Path(__file__).resolve().parent
TOOLCHAIN = "zephyr-sdk-0.16.5/arm-zephyr-eabi"
LIBGCC = "lib/gcc/arm-zephyr-eabi/12.2.0/thumb/v7e-m/nofp/libgcc.a"
FLAGS = frozenset("""
--param=min-pagesize=0 -Os -Wall -Werror -Werror=implicit-int -Wexpansion-to-defined
-Wextra -Wformat -Wformat-security -Wno-format-zero-length -Wno-pointer-sign
-Wno-unused-but-set-variable -Wpointer-arith -Wshadow -c -MD -fdata-sections
-fdiagnostics-color=always -ffreestanding -ffunction-sections
-fno-asynchronous-unwind-tables -fno-builtin -fno-builtin-malloc -fno-common
-fno-defer-pop -fno-lto -fno-pic -fno-pie -fno-reorder-functions -fno-strict-aliasing
-g -gdwarf-4 -mabi=aapcs -mcpu=cortex-m4 -mfp16-format=ieee -mthumb
-nostdinc -std=c99 -xassembler-with-cpp
-DKERNEL -DNRF52840_XXAA -D_ASMLANGUAGE -D__PROGRAM_START -D__ZEPHYR_SUPERVISOR__
-D__ZEPHYR__=1 -DNRF_802154_ACK_TIMEOUT_ENABLED=1 -DNRF_802154_CARRIER_FUNCTIONS_ENABLED=0
-DNRF_802154_CCA_CORR_LIMIT_DEFAULT=2 -DNRF_802154_CCA_CORR_THRESHOLD_DEFAULT=45
-DNRF_802154_CCA_ED_THRESHOLD_DEFAULT=45 -DNRF_802154_CCA_MODE_DEFAULT=NRF_RADIO_CCA_MODE_ED
-DNRF_802154_CSMA_CA_ENABLED=0 -DNRF_802154_DELAYED_TRX_ENABLED=0 -DNRF_802154_ECB_PRIORITY=-1
-DNRF_802154_ENCRYPTION_ENABLED=0 -DNRF_802154_ENERGY_DETECTED_VERSION=1
-DNRF_802154_FRAME_TIMESTAMP_ENABLED=0 -DNRF_802154_IE_WRITER_ENABLED=0
-DNRF_802154_IFS_ENABLED=0 -DNRF_802154_INTERNAL_RADIO_IRQ_HANDLING=1
-DNRF_802154_PENDING_EXTENDED_ADDRESSES=16 -DNRF_802154_PENDING_SHORT_ADDRESSES=16
-DNRF_802154_PLATFORM_ASSERT_INCLUDE="nrf_802154_assert_zephyr.h"
-DNRF_802154_RX_BUFFERS=4 -DNRF_802154_SECURITY_KEY_STORAGE_SIZE=3
-DNRF_802154_SECURITY_WRITER_ENABLED=0 -DNRF_802154_SERIALIZATION_HOST=0
-DNRF_802154_SWI_PRIORITY=1 -DNRF_802154_TX_STARTED_NOTIFY_ENABLED=1 -DNRF_802154_USE_RAW_API=1
""".split())


def environment_check() -> None:
    for name in ("GCC_EXEC_PREFIX", "COMPILER_PATH", "LIBRARY_PATH", "CPATH", "C_INCLUDE_PATH",
                 "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH", "LD_PRELOAD", "LD_LIBRARY_PATH",
                 "GCC_COMPARE_DEBUG", "DEPENDENCIES_OUTPUT", "SUNPRO_DEPENDENCIES"):
        artifact.require(name not in os.environ, f"unreviewed tool environment: {name}")


def checked_digest(path: Path, root: Path, expected: str) -> str:
    resolved = path.resolve(strict=True)
    artifact.require(resolved.is_relative_to(root.resolve()) and resolved.is_file(),
                     f"file outside trusted root or not regular: {path}")
    actual = artifact.digest(resolved.read_bytes())
    artifact.require(actual == expected, f"vetted digest mismatch: {path}")
    return actual


def audit_tools(sdk: Path) -> tuple[dict, dict]:
    lock = json.loads((HERE / "audit-tools.json").read_text())
    identities, executables = {}, {}
    for relative, expected in lock["arm_release"]["files"].items():
        path = sdk / "zephyr-sdk-0.16.5" / relative
        identities[str(path)] = checked_digest(path, sdk / "zephyr-sdk-0.16.5", expected)
    for name, entry in lock["wheels"].items():
        candidates = list((sdk / "venv/lib").glob("python*/site-packages/" + entry["member"]))
        artifact.require(len(candidates) == 1, f"expected one vetted native {name}")
        path = candidates[0].resolve()
        identities[str(path)] = checked_digest(path, sdk / "venv", entry["executable_sha256"])
        executables[name] = path
    return identities, executables


def compile_command(item: dict, sdk: Path, build: Path) -> tuple[list[str], Path, Path]:
    require = artifact.require
    require(isinstance(item, dict) and set(item) <= {"file", "directory", "command", "output"}
            and all(isinstance(item.get(k), str) for k in ("file", "directory", "command")),
            "unreviewed compilation metadata shape")
    require(all(ord(c) >= 32 and ord(c) < 127 for c in item["command"]), "control/non-ASCII command")
    cwd = Path(item["directory"])
    require(cwd.is_absolute() and cwd.resolve() == build and cwd.is_dir(),
            "compiler cwd must be the selected build directory")
    args = shlex.split(item["command"])
    compiler = sdk / TOOLCHAIN / "bin/arm-zephyr-eabi-gcc"
    require(bool(args) and args[0] == str(compiler), "unreviewed compiler path")
    roots = [HERE, build, sdk / TOOLCHAIN] + [sdk / n for n in
                                             ("nrf", "zephyr", "nrfxlib", "hal_nordic", "cmsis")]

    def path_in_roots(value):
        path = (cwd / value).resolve()
        require(any(path.is_relative_to(root) for root in roots), f"path outside pinned roots: {value}")
        return path

    source = Path(item["file"])
    require(source.is_absolute(), "relative compilation source")
    source = path_in_roots(str(source))
    require(source.is_file() and source.suffix in (".c", ".S"), "unreviewed source type/path")
    dynamic = {
        f'--sysroot={sdk / TOOLCHAIN / "arm-zephyr-eabi"}',
        f"-fmacro-prefix-map={HERE}=CMAKE_SOURCE_DIR",
        f"-fmacro-prefix-map={sdk / 'zephyr'}=ZEPHYR_BASE",
        f'-DNRF_802154_PROJECT_CONFIG="{HERE / "radio_config.h"}"',
    }
    macro_headers = {(build / "zephyr/include/generated/autoconf.h").resolve(),
                     (sdk / "zephyr/include/zephyr/toolchain/zephyr_stdint.h").resolve()}
    paths, sources, index = {}, [], 1
    while index < len(args):
        arg = args[index]
        if arg in ("-o", "-MT", "-MF", "-isystem", "-imacros"):
            require(index + 1 < len(args), "missing compiler operand")
            path = path_in_roots(args[index + 1])
            if arg in ("-isystem", "-imacros"):
                require(path.is_dir() if arg == "-isystem" else path in macro_headers,
                        f"unreviewed {arg} path")
            else:
                require(arg not in paths and path.is_relative_to(build), "duplicate/escaped output")
                paths[arg] = path
            index += 2
            continue
        if arg.startswith("-I"):
            require(len(arg) > 2 and path_in_roots(arg[2:]).is_dir(), "invalid include path")
        elif arg in FLAGS or arg in dynamic:
            pass
        elif not arg.startswith("-") and Path(arg).is_absolute() and path_in_roots(arg) == source:
            sources.append(source)
        else:
            raise artifact.ArtifactError(f"unreviewed compiler flag/operand: {arg}")
        index += 1
    require(args.count("-c") == 1 and sources == [source] and "-o" in paths,
            "expected one source, -c and object output")
    obj = paths["-o"]
    require(obj.suffix == ".obj" and paths.get("-MT", obj) == obj and
            paths.get("-MF", Path(str(obj) + ".d")) == Path(str(obj) + ".d"), "invalid object/dependency output")
    if "output" in item:
        require(isinstance(item["output"], str) and path_in_roots(item["output"]) == obj,
                "output metadata differs from command")
    return args, source, obj


def startup_order(disassembly: str) -> dict:
    require = artifact.require
    match = re.search(r"<main>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)", disassembly, re.DOTALL)
    require(match is not None, "missing linked main")
    instructions = [(int(address, 16), op, operands) for address, op, operands in re.findall(
        r"^[ \t]*([0-9a-f]+):[ \t]+(?:[0-9a-f]{4}[ \t]+)+(\S+)[ \t]+([^\n]*)$",
        match[1], re.MULTILINE)]

    def call_index(name):
        found = [i for i, (_, op, operands) in enumerate(instructions)
                 if op in ("bl", "bl.w") and operands.endswith("<" + name + ">")]
        require(len(found) == 1, f"expected one main call to {name}")
        return found[0]

    begin = call_index("stim_startup_begin")
    init = call_index("nrf_802154_init")
    complete = call_index("stim_startup_complete")
    require(begin < init < complete, "wrong linked startup order")
    between = instructions[begin + 1:init]
    require(len(between) == 3 and between[0][1:] == ("dmb", "sy") and
            between[1][1] == "msr" and between[1][2].startswith("PRIMASK, r") and
            between[2][1] == "cbz", "SDK initialization not guarded by begin result")
    branch = re.fullmatch(r"r0, ([0-9a-f]+) <main\+0x[0-9a-f]+>", between[2][2])
    require(branch is not None and complete + 3 < len(instructions), "unreviewed cold-failure branch")
    after = instructions[complete + 1:complete + 3]
    require(after[0][1:] == ("dmb", "sy") and after[1][1] == "msr" and
            after[1][2].startswith("PRIMASK, r") and
            int(branch[1], 16) == instructions[complete + 3][0],
            "cold failure does not skip both SDK init and completion")
    require(any(op in ("bl", "bl.w") and operands.endswith("<radio_stopped>")
                for _, op, operands in instructions[init + 1:complete]),
            "missing fresh post-init stopped observation")
    require(not any("<stim_init>" in operands for _, _, operands in instructions),
            "main must not erase callback/fault evidence")
    return {"begin_call": instructions[begin][0], "sdk_init_call": instructions[init][0],
            "post_init_publication": instructions[complete][0],
            "cold_failure_skip_target": int(branch[1], 16)}


def command(args, cwd=None) -> str:
    return subprocess.check_output(args, cwd=cwd, text=True, timeout=120)


def preprocess(item: dict, macros: bool, sdk: Path, build: Path) -> str:
    environment_check()
    original, _, _ = compile_command(item, sdk, build)
    args, skip = [], False
    for arg in original:
        if skip:
            skip = False
        elif arg in ("-o", "-MT", "-MF"):
            skip = True
        elif arg not in ("-c", "-MD"):
            args.append(arg)
    return command(args + ["-E", "-dM" if macros else "-P"], item["directory"])


def audit(sdk: Path, build: Path) -> dict:
    require = artifact.require
    environment_check()
    require(not build.is_relative_to(HERE.parents[1]), "target artifacts must stay outside repository")
    require(not sdk.is_relative_to(HERE.parents[1]), "SDK must stay outside repository")
    identities, native_tools = audit_tools(sdk)
    entries = json.loads((build / "compile_commands.json").read_text())
    validated = [compile_command(item, sdk, build) for item in entries]
    lock = json.loads((HERE / "dependencies.json").read_text())
    patch = (HERE / "phyend-observer.patch").read_text()
    for name, (_, revision) in lock["sources"].items():
        repo = sdk / name
        require(command(["git", "-C", str(repo), "rev-parse", "HEAD"]).strip() == revision,
                f"{name}: revision mismatch")
        status = command(["git", "-C", str(repo), "status", "--porcelain", "--untracked-files=no"])
        if name == "nrfxlib":
            require(status.strip() == "M nrf_802154/driver/src/nrf_802154_core.c",
                    "unexpected nrfxlib modifications")
            diff = command(["git", "-C", str(repo), "--no-pager", "diff", "--no-ext-diff",
                            "--no-textconv", "--",
                            "nrf_802154/driver/src/nrf_802154_core.c"])
            require(patch[patch.index("diff --git "):] == diff, "patch differs from actual source")
        else:
            require(not status, f"modified {name} source")
    dry = command([str(native_tools["ninja"]), "-C", str(build), "-n", "zephyr_final"])
    require("ninja: no work to do." in dry and "[1/" not in dry, "target build is stale")
    elf = (build / "zephyr/zephyr.elf").read_bytes()
    hexb = (build / "zephyr/zephyr.hex").read_bytes()
    image = artifact.compare(elf, hexb.decode("ascii"))
    config = (build / "zephyr/.config").read_text()
    dts = (build / "zephyr/zephyr.dts").read_text()
    artifact.configuration(config, dts)
    toolbin = sdk / "zephyr-sdk-0.16.5/arm-zephyr-eabi/bin"
    compiler = toolbin / "arm-zephyr-eabi-gcc"
    inventory, object_hashes, unbuilt = [], set(), []
    for item, (args, source, obj) in zip(entries, validated):
        require(not re.search(r"/(?:drivers/flash|mpsl)/|/(?:flash_nrf|nrfx_nvmc)\.c$",
                              str(source)), "forbidden radio/flash source")
        if not obj.exists():
            unbuilt.append({"source": str(source), "object": str(obj),
                            "status": "declared by CMake, not built or accepted as link evidence"})
            continue
        sha = artifact.digest(obj.read_bytes())
        object_hashes.add(sha)
        header = source.read_text()[:6000]
        license_match = re.search(r"SPDX-License-Identifier:\s*([^\r\n*]+)", header)
        inventory.append({"source": str(source), "source_sha256": artifact.digest(source.read_bytes()),
                          "object": str(obj), "object_sha256": sha,
                          "spdx": license_match[1].strip() if license_match else None,
                          "command_sha256": artifact.digest(item["command"].encode())})

    start = next(e for e in entries if e["file"].endswith("/system_nrf52840.c"))
    core = next(e for e in entries if e["file"].endswith("/nrf_802154_core.c"))
    require("/nrfxlib/nrf_802154/driver/src/" in core["file"], "wrong driver copy")
    pp, macros = preprocess(start, False, sdk, build), preprocess(start, True, sdk, build)
    startup_hashes = artifact.startup(macros, pp)
    baseline = build / "original-nrf_802154_core.c"
    baseline.write_bytes(subprocess.check_output(
        ["git", "-C", str(sdk / "nrfxlib"), "show",
         "HEAD:nrf_802154/driver/src/nrf_802154_core.c"], timeout=30))
    normal_args = [a for a in shlex.split(core["command"])
                   if not a.startswith("-DNRF_802154_PROJECT_CONFIG=")]
    normal = dict(core, command=shlex.join(normal_args))
    original = dict(core, file=str(baseline), command=shlex.join(
        [str(baseline) if a == core["file"] else a for a in normal_args]))
    normal_pp = preprocess(normal, False, sdk, build)
    original_pp = preprocess(original, False, sdk, build)

    def normalized(text):
        return "\n".join(line.strip() for line in text.splitlines() if line.strip())

    require(normalized(normal_pp) == normalized(original_pp),
            "default-off complete core differs from unmodified upstream")
    normal_hash = artifact.digest(normalized(normal_pp).encode())
    radio_macros = preprocess(core, True, sdk, build)
    required = {"NS51_DIAGNOSTIC_PHYEND": "1", "NRF_802154_USE_RAW_API": "1",
                "NRF_802154_INTERNAL_RADIO_IRQ_HANDLING": "1",
                "NRF_802154_IRQ_PRIORITY": "0", "NRF_802154_ACK_TIMEOUT_ENABLED": "1",
                "NRF_802154_CSMA_CA_ENABLED": "0", "NRF_802154_DELAYED_TRX_ENABLED": "0",
                "NRF_802154_IFS_ENABLED": "0", "NRF_802154_FRAME_TIMESTAMP_ENABLED": "0",
                "NRF_802154_ENCRYPTION_ENABLED": "0", "NRF_802154_SECURITY_WRITER_ENABLED": "0",
                "NRF_802154_IE_WRITER_ENABLED": "0", "NRF_802154_CARRIER_FUNCTIONS_ENABLED": "0"}
    for name, value in required.items():
        require(re.search(r"^#define " + name + " " + value + "$", radio_macros, re.MULTILINE)
                is not None, f"actual radio macro {name} is not {value}")
    source_names = [x["source"] for x in inventory]
    for suffix in ("/nrf_802154_request_direct.c", "/nrf_802154_notification_direct.c",
                   "/sl_opensource/src/nrf_802154_sl_timer.c",
                   "/sl_opensource/src/nrf_802154_sl_rsch.c",
                   "/sl_opensource/platform/nrf_802154_irq_zephyr.c"):
        require(sum(p.endswith(suffix) for p in source_names) == 1, f"missing/duplicate {suffix}")

    # Every linked archive member must equal an object from an actual source
    # compilation. Only the verified compiler's ordinary libgcc is excepted.
    map_text = (build / "zephyr/zephyr.map").read_text()
    archives = []
    direct_objects = []
    loads = sorted(set(re.findall(r"^LOAD (.+)$", map_text, re.MULTILINE)))
    for name in loads:
        if name == "linker stubs":
            stub_lines = [line for line in map_text.splitlines()
                          if line.endswith("linker stubs") and not line.startswith("LOAD ")]
            stubs = re.findall(r"^ (\.\S+)\s+0x[0-9a-f]+\s+(0x[0-9a-f]+) linker stubs$",
                               map_text, re.MULTILINE)
            require(len(stub_lines) == len(stubs) == 4 and
                    {name for name, _ in stubs} == {".glue_7", ".glue_7t", ".vfp11_veneer", ".v4_bx"}
                    and all(int(size, 16) == 0 for _, size in stubs),
                    "nonempty/unreviewed linker-generated veneers")
            direct_objects.append({"kind": "linker-generated empty veneers", "sections": dict(stubs)})
            continue
        path = (build / name).resolve()
        if path.suffix == ".a":
            continue
        require(path.suffix in (".o", ".obj") and path.is_relative_to(build),
                f"unreviewed direct link input {name}")
        sha = artifact.digest(path.read_bytes())
        require(sha in object_hashes, f"direct object without source compilation {name}")
        direct_objects.append({"path": str(path), "sha256": sha})
    for name in sorted(set(re.findall(r"^LOAD (.+\.a)$", map_text, re.MULTILINE))):
        path = (build / name).resolve()
        if path.is_relative_to(sdk / "zephyr-sdk-0.16.5"):
            require(path == (sdk / TOOLCHAIN / LIBGCC).resolve() and str(path) in identities,
                    "unexpected/unvetted precompiled runtime")
            members = ["toolchain libgcc; not Nordic radio/SL/MPSL"]
        else:
            require(path.is_relative_to(build), "precompiled external library")
            members = command([str(toolbin / "arm-zephyr-eabi-ar"), "t", str(path)]).splitlines()
            require(0 < len(members) <= 512 and len(members) == len(set(members)),
                    "invalid/duplicate archive members")
            for member in members:
                require(re.fullmatch(r"[\w.+-]+\.obj", member) is not None, "unexpected member")
                blob = subprocess.check_output([str(toolbin / "arm-zephyr-eabi-ar"),
                                                "p", str(path), member], timeout=30)
                require(artifact.digest(blob) in object_hashes, f"unproven archive member {member}")
        archives.append({"path": str(path), "sha256": artifact.digest(path.read_bytes()),
                         "members": members})
    require(any("sl/sl_opensource/libnrf-802154-sl.a" in a["path"] for a in archives),
            "open-source SL not actually linked")
    nm = command([str(toolbin / "arm-zephyr-eabi-nm"), "-S", str(build / "zephyr/zephyr.elf")])
    for name in ("SystemInit", "ns51_phy_tx_done", "ns51_observer_asleep",
                 "ns51_observer_profile_valid", "nrf_802154_trx_transmit_frame_transmitted"):
        require(re.search(r" T " + name + "$", nm, re.MULTILINE) is not None,
                f"missing linked {name}")
    require(re.search(r" [Tt] (?:nrf_nvmc_|nrfx_nvmc_|flash_erase|flash_write|sys_reboot)", nm) is None,
            "linked writer/reset helper")
    disassembly = command([str(toolbin / "arm-zephyr-eabi-objdump"), "-d",
                           str(build / "zephyr/zephyr.elf")])
    boot_order = startup_order(disassembly)
    transition = re.search(
        r"<nrf_802154_trx_transmit_frame_transmitted>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)",
        disassembly, re.DOTALL)
    require(transition is not None, "missing PHYEND disassembly")
    calls = re.findall(r"\bbl(?:\.w)?\s+[0-9a-f]+ <([^>]+)>", transition[1])
    require(calls == ["switch_to_idle", "nrf_802154_critical_section_nesting_allow",
                      "nrf_802154_core_hooks_transmitted",
                      "nrf_802154_tx_work_buffer_original_frame_update",
                      "ns51_phy_tx_done", "nrf_802154_critical_section_nesting_deny"],
            "actual PHYEND call sequence differs")
    require("nrf_802154_trx_receive_ack" not in transition[1] and
            "nrf_802154_notify_transmitted" not in transition[1], "ACK-success substitution")
    require(re.search(r"\bb(?:l)?(?:\.w)?\s+[0-9a-f]+ <SystemInit>", disassembly) is not None and
            re.search(r"\bbl(?:\.w)?\s+[0-9a-f]+ <z_arm_platform_init>", disassembly) is not None,
            "SystemInit not reachable through linked startup calls/tail branches")
    require(re.search(r" [Tt] (?:nrf_802154_sl_ecb_block_encrypt|wait_for_ecb_end)$",
                      nm, re.MULTILINE) is None, "unused blocking ECB code unexpectedly linked")
    require(protocol.DESCRIPTOR in elf and b"\x16" + protocol.BODY + b"\0\0" in elf,
            "descriptor/fixed AR frame missing from linked image")
    for filename, text in (("startup.i", pp), ("startup.macros", macros),
                           ("radio.macros", radio_macros), ("linked.dis", disassembly)):
        (build / filename).write_text(text)
    return {
        "schema": 1, "evidence": "build/static only; no Cortex-M simulation or hardware",
        "trust_boundary": "Trusted locally built workspace and host, not arbitrary downloaded metadata",
        "vetted_tool_digests": identities,
        "audit_tools_lock_sha256": artifact.digest((HERE / "audit-tools.json").read_bytes()),
        "sources": lock["sources"], "board": lock["board"],
        "compiler": command([str(compiler), "--version"]).splitlines()[0],
        "cmake": command([str(native_tools["cmake"]), "--version"]).splitlines()[0],
        "ninja": command([str(native_tools["ninja"]), "--version"]).strip(),
        "sdk": lock["toolchain"], "patch_sha256": artifact.digest(patch.encode()),
        "descriptor_sha256": protocol.DESCRIPTOR.hex(),
        "elf_sha256": artifact.digest(elf), "hex_sha256": artifact.digest(hexb),
        "map_sha256": artifact.digest(map_text.encode()),
        "config_sha256": artifact.digest(config.encode()), "dts_sha256": artifact.digest(dts.encode()),
        "compile_commands_sha256": artifact.digest((build / "compile_commands.json").read_bytes()),
        "flash_extent": image.flash_extent, "flash_load_bytes": len(image.memory),
        "sram_allocated": image.sram_allocated, "sram_extent": image.sram_extent,
        "sections": image.sections, "startup": startup_hashes,
        "firmware_startup_order": boot_order,
        "default_off_whole_core_preprocessed_sha256": normal_hash,
        "radio_macros": required, "phyend_calls": calls,
        "archives": archives, "direct_objects": direct_objects,
        "objects": inventory, "declared_unbuilt": unbuilt,
        "application": {p.name: artifact.digest(p.read_bytes()) for p in sorted(HERE.iterdir())
                        if p.is_file() and p.suffix in (".c", ".h", ".conf", ".overlay")},
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--build", required=True, type=Path)
    args = parser.parse_args()
    try:
        result = audit(args.sdk.resolve(), args.build.resolve())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, f"NS51 static audit FAILED: {error}\n")
    output = args.build / "evidence.json"
    output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: result[k] for k in
                     ("compiler", "elf_sha256", "hex_sha256", "flash_extent",
                      "flash_load_bytes", "sram_allocated", "sram_extent")}, indent=2))
    print(f"Static evidence: {output}")


if __name__ == "__main__":
    main()
