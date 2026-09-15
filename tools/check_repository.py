#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Small M0 publication guardrails, not a complete secret/provenance scanner."""

import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN_SUFFIXES = {
    ".bin", ".hex", ".ihx", ".ram", ".dump", ".pcap", ".pcapng",
    ".log", ".key", ".pem", ".p12", ".pfx", ".r51", ".d51",
}
SECRET_PATTERNS = (
    re.compile(r"gh[pousr]_[A-Za-z0-9]{20,}"),
    re.compile(r"github_pat_[A-Za-z0-9_]{40,}"),
    re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----"),
)
PERSONAL_PATH = re.compile(r"/(?:Users|home)/[A-Za-z0-9_.-]+/")
MARKDOWN_LINK = re.compile(r"!?\[[^\]]*\]\(([^)\n]+)\)")


def repository_paths():
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=ROOT, capture_output=True, check=True,
    )
    return sorted({
        Path(name.decode("utf-8"))
        for name in result.stdout.split(b"\0") if name
    })


def inspect_file(relative):
    path = ROOT / relative
    errors = []
    if path.is_symlink():
        return [f"{relative}: symbolic links are not allowed in the M0 source tree"]
    if not path.is_file():
        return [f"{relative}: expected a regular source file"]
    if path.suffix.lower() in FORBIDDEN_SUFFIXES:
        errors.append(f"{relative}: generated/private/proprietary file type")
    if path.name == ".env" or path.name.startswith(".env."):
        errors.append(f"{relative}: environment files must not be published")
    if path.stat().st_size > 256 * 1024:
        return errors + [f"{relative}: source file exceeds the M0 size guard"]
    data = path.read_bytes()
    if b"\0" in data:
        return errors + [f"{relative}: binary assets require a separate reviewed policy"]
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return errors + [f"{relative}: source files must be UTF-8 text"]
    if PERSONAL_PATH.search(text):
        errors.append(f"{relative}: personal absolute path; use a generic/relative path")
    for pattern in SECRET_PATTERNS:
        if pattern.search(text):
            errors.append(f"{relative}: credential-like content; value is not printed")
    if path.suffix.lower() == ".md":
        errors.extend(inspect_links(relative, text))
    return errors


def inspect_links(relative, text):
    errors = []
    for match in MARKDOWN_LINK.finditer(text):
        target = match.group(1).strip()
        if target.startswith("<") and target.endswith(">"):
            target = target[1:-1]
        parsed = urlsplit(target)
        if parsed.scheme or parsed.netloc or not parsed.path:
            continue
        destination = (ROOT / relative.parent / unquote(parsed.path)).resolve()
        if not destination.is_relative_to(ROOT):
            errors.append(f"{relative}: documentation link escapes the repository")
        elif not destination.exists():
            errors.append(f"{relative}: missing local link target {parsed.path}")
    return errors


def main():
    paths = repository_paths()
    if not paths:
        raise SystemExit("No source files found; publication checks did not run")
    errors = [error for path in paths for error in inspect_file(path)]
    if errors:
        print("\n".join(errors), file=sys.stderr)
        raise SystemExit(1)
    print(f"Repository guardrails/local link checks passed for {len(paths)} files.")


if __name__ == "__main__":
    main()
