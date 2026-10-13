# SPDX-License-Identifier: BSD-3-Clause
"""Derive application observation regions from the exact target C layout.

The compiler probe is never linked into firmware or used to write DUT state.
"""
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys

from elftools.elf.elffile import ELFFile


def layout(output):
    output = Path(output).resolve()
    command = (output / "compile-command.txt").read_text().split(" -c SRC -o OBJ", 1)[0]
    flags = shlex.split(command)
    probe = '#include <stddef.h>\n#include "zcl_sensor.h"\n'
    expressions = {
        "device_size": "sizeof(bdb_join_t)",
        "reports_offset": "offsetof(bdb_join_t,record)+offsetof(zcl_sensor_state_t,report)",
        "reports_size": "sizeof(((zcl_sensor_state_t *)0)->report)",
        "bindings_offset": "offsetof(bdb_join_t,record)+offsetof(zcl_sensor_state_t,bound)",
        "bindings_size": "sizeof(((zcl_sensor_state_t *)0)->bound)",
        "flags_offset": "offsetof(bdb_join_t,record)+offsetof(zcl_sensor_state_t,basic)",
        "flags_size": "offsetof(zcl_sensor_state_t,tsn)-offsetof(zcl_sensor_state_t,basic)+1",
    }
    probe += "\n".join(f"const uint16_t rrc_{name}={value};" for name, value in expressions.items())
    ir = subprocess.run(flags + ["-S", "-emit-llvm", "-x", "c", "-", "-o", "-"],
                        cwd=output / "src", input=probe, text=True, stdout=subprocess.PIPE, check=True).stdout
    values = {name: int(value) for name, value in
              re.findall(r"^@rrc_(\w+) = [^\n]*constant i16 (\d+)", ir, re.M)}
    if set(values) != set(expressions):
        raise ValueError("Missing exact target reporting-layout constants")
    elf_path = output / "link/hw1.elf"
    with elf_path.open("rb") as stream:
        elf = ELFFile(stream)
        owners = elf.get_section_by_name(".symtab").get_symbol_by_name("join_smoke_device")
        if len(owners or []) != 1 or owners[0]["st_size"] != values["device_size"]:
            raise ValueError("Target layout does not describe the linked application owner")
    regions = []
    for name, size in (("reports", 40), ("bindings", 3), ("flags", 4)):
        offset = values[name + "_offset"]
        if values[name + "_size"] != size or not 0 <= offset <= values["device_size"] - size:
            raise ValueError(f"Invalid target application observation region: {name}")
        regions.append({"name": name, "offset": offset, "size": size})
    (output / "audit/sensor-layout.ll").write_text(ir)
    result = {"elf_sha256": hashlib.sha256(elf_path.read_bytes()).hexdigest(),
              "clang_sha256": hashlib.sha256(Path(flags[0]).read_bytes()).hexdigest(),
              "device_size": values["device_size"], "regions": regions}
    (output / "audit/sensor-layout.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


if __name__ == "__main__":
    layout(sys.argv[1])
