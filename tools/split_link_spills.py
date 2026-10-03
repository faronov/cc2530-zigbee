#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Split only compiler spill allocations; this is NOT an IRAM placement proof."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from verify_firmware import require


AREA = re.compile(r"(?m)^[ \t]*\.area[ \t]+(\w+)[ \t]*\(DATA\)[ \t]*\n")
SPILL = re.compile(
    r"(?m)^L(\w+)\.(\w+)\$sloc(\d+)\$(\d+)_(\d+)\$0==\.\n"
    r"_(\w+)_sloc(\d+)_(\d+)_(\d+):\n[ \t]*\.ds (\d+)[ \t]*\n")
HEADER_LINE = re.compile(
    r"(?m)^([ \t]*)(C\$([\w.]+\.h)\$\d+\$\d+_\d+\$\d+)([ \t]*==\.[ \t]*)$")


def comments(text):
    return all(not line.strip() or line.lstrip().startswith(";") for line in text.splitlines())


def split_assembly(raw):
    require(b"\r" not in raw, "Noncanonical compiler assembly")
    text = raw.decode("ascii")
    modules = re.findall(r"(?m)^\s*\.module\s+(\w+)\s*$", text)
    require(len(modules) == 1 and
            len(re.findall(r"(?m)^\s*\.optsdcc -mmcs51 --model-large\s*$", text)) == 1,
            "Expected one large-model mcs51 compiler module")
    module = modules[0]
    require(not re.search(r"(?m)^\s*\.area\s+JF_", text), "Assembly was already split")
    areas = [m for m in AREA.finditer(text) if m[1].startswith("JD_")]
    if module == "banked":
        require(not areas, "Banker must keep its real DATA reservation")
        return raw, {}
    require(len(areas) == 1, f"Missing or ambiguous draft compiler DATA area in {module}")
    start = areas[0].end()
    following = re.search(r"(?m)^[ \t]*\.area\s+", text[start:])
    require(following is not None, "Unterminated compiler DATA area")
    end = start + following.start()
    section = text[start:end]
    pieces, inserted, widths, seen = [], [], {}, set()
    previous, cursor = None, 0
    for match in SPILL.finditer(section):
        require(comments(section[cursor:match.start()]), "Non-spill DATA allocation")
        require(match[1] == module and match[2] == match[6] and match[3] == match[7] and
                match[4] == match[9] and match[5] == match[8], "Spill/debug identity mismatch")
        function, size = match[2], int(match[10])
        require(1 <= size <= 4, "Unexpected compiler spill width")
        key = (function, match[3])
        require(key not in seen, "Duplicate compiler spill")
        seen.add(key)
        pieces.append(section[cursor:match.start()])
        area = f"JF_{module}_{function}"
        if function != previous:
            require(area not in widths, "Noncontiguous function spill allocation")
            directive = f"\t.area {area} (DATA)\n"
            pieces.append(directive)
            inserted.append(directive)
            previous = function
        widths[area] = widths.get(area, 0) + size
        pieces.append(match[0])
        cursor = match.end()
    require(comments(section[cursor:]), "Unclassified compiler DATA contents")
    pieces.append(section[cursor:])
    result = text[:start] + "".join(pieces) + text[end:]
    reconstructed = result
    for directive in inserted:
        require(text.count(directive) == 0 and reconstructed.count(directive) == 1,
                "Ambiguous inserted DATA directive")
        reconstructed = reconstructed.replace(directive, "", 1)
    require(reconstructed == text, "Transformation changed more than DATA area directives")
    return result.encode("ascii"), widths


def scope_header_lines(raw):
    """Retain shared-header debug locations without cross-object collisions."""
    text = raw.decode("ascii")
    modules = re.findall(r"(?m)^\s*\.module\s+(\w+)\s*$", text)
    require(len(modules) == 1, "Missing source-line module identity")
    replacements = {}

    def rename(match):
        old = match[2]
        new = old.replace("C$", "C$" + modules[0] + ".", 1)
        require(new not in text, "Header source-line namespace already exists")
        replacements[old] = new
        return match[1] + new + match[4]

    output = HEADER_LINE.sub(rename, text)
    restored = output
    for old, new in replacements.items():
        require(restored.count(new) == text.count(old), "Header label used outside a source-line definition")
        restored = restored.replace(new, old)
    require(restored == text, "Source-line scoping changed non-debug assembly")
    return output.encode("ascii"), replacements


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--assembler", default="sdas8051")
    args = parser.parse_args()
    require(args.input.resolve() != args.output.resolve(), "Compiler originals must remain intact")
    inputs = sorted(args.input.glob("*.asm"))
    require(inputs, "No genuine compiler assembly")
    prepared = []
    for source in inputs:
        raw = source.read_bytes()
        transformed, widths = split_assembly(raw)
        transformed, header_lines = scope_header_lines(transformed)
        debug = source.with_suffix(".adb").read_bytes()
        prepared.append((source, raw, transformed, widths, header_lines, debug))
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for source, raw, transformed, widths, header_lines, debug in prepared:
        destination = args.output / source.name
        require(not destination.is_symlink() and not destination.with_suffix(".adb").is_symlink(),
                "Output is a symlink")
        destination.write_bytes(transformed)
        destination.with_suffix(".adb").write_bytes(debug)
        subprocess.run([args.assembler, "-plosgff", str(destination)], check=True)
        manifest[source.stem] = {
            "compiler_asm": hashlib.sha256(raw).hexdigest(),
            "split_asm": hashlib.sha256(transformed).hexdigest(),
            "debug": hashlib.sha256(debug).hexdigest(),
            "areas": widths,
            "header_lines": header_lines,
        }
    (args.output / "spill-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Split {sum(len(m['areas']) for m in manifest.values())} compiler function frames; "
          "physical reservations, call liveness and stack are NOT yet verified.")


if __name__ == "__main__":
    main()
