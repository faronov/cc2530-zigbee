# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic archive/cache rejection controls; no network or MCU execution."""
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

import xdata_toolchain as tool
from join_smoke_image import runtime_map_identity


class ToolchainTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.cache = self.root / "cache"
        self.pins = tool.load_pins()
        self.package = self.root / "source"
        self.package.mkdir()
        for relative in ([f"bin/{n}" for n in tool.BINS] +
                         [f"share/sdcc/lib/large/{n}.lib" for n in tool.LIBS]):
            path = self.package / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"synthetic package, never executed\n")
            path.chmod(0o755 if relative.startswith("bin/") else 0o644)
        (self.package / "BUILDINFO.txt").write_text(json.dumps(dict(
            release=self.pins["tag"], source_commit=self.pins["source_commit"],
            ownership_schema=1, platform="linux-x86_64")))
        (self.package / "BUILDINFO.txt").chmod(0o644)
        for directory in (self.package, *[p for p in self.package.rglob("*") if p.is_dir()]):
            directory.chmod(0o755)
        self.pins["executable_sha256"] = tool.sha(self.package / "bin/sdcc")
        self.manifest()
        self.archive = self.root / self.pins["asset"]
        with tarfile.open(self.archive, "w:xz") as archive:
            archive.add(self.package, arcname=self.pins["asset"].removesuffix(".tar.xz"))
        self.pins["archive_sha256"] = tool.sha(self.archive)
        capability = patch.object(tool, "capability", return_value={"schema": 1})
        self.probe = capability.start()
        self.addCleanup(capability.stop)
        download = patch.object(tool, "download",
                                side_effect=lambda pins, path: shutil.copyfile(self.archive, path))
        self.download = download.start()
        self.addCleanup(download.stop)

    def manifest(self):
        value = {"version": 1, "files": {
            str(p.relative_to(self.package)): dict(sha256=tool.sha(p), mode=p.stat().st_mode & 0o777)
            for p in self.package.rglob("*") if p.is_file() and p.name != "MANIFEST.json"}}
        path = self.package / "MANIFEST.json"
        path.write_text(json.dumps(value))
        path.chmod(0o644)
        self.pins["manifest_sha256"] = tool.sha(path)

    def prepare(self, **kwargs):
        return tool.prepare(cache=self.cache, pins=self.pins, **kwargs)

    def test_download_then_offline_reuse_never_redownloads(self):
        first = self.prepare()
        with patch.object(tool.urllib.request, "urlopen", side_effect=urllib.error.URLError("offline")):
            self.assertEqual(first, self.prepare(offline=True))
        self.assertEqual(self.download.call_count, 1)
        self.assertEqual(first[1]["mode"], "release")

    def test_missing_pinned_compiler_never_uses_system_sdcc_offline(self):
        with patch.object(tool.shutil, "which", return_value="/usr/bin/sdcc"), \
                self.assertRaisesRegex(ValueError, "unavailable offline"):
            self.prepare(offline=True)
        self.download.assert_not_called()
        self.probe.assert_not_called()

    def test_wrong_archive_hash_and_correct_filename_wrong_contents(self):
        self.pins["archive_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "SHA256"):
            self.prepare()
        self.assertFalse((self.cache / self.pins["tag"] / "toolchain").exists())

    def test_truncated_archive_even_with_matching_test_digest(self):
        self.archive.write_bytes(self.archive.read_bytes()[:80])
        self.pins["archive_sha256"] = tool.sha(self.archive)
        with self.assertRaises((tarfile.TarError, EOFError)):
            self.prepare()

    def test_corrupt_cached_archive_is_rejected_then_deliberately_repaired(self):
        self.prepare()
        archive = self.cache / self.pins["tag"] / self.pins["asset"]
        archive.write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "Cached archive corrupt"):
            self.prepare()
        with self.assertRaisesRegex(ValueError, "unavailable offline"):
            self.prepare(offline=True, repair=True)
        self.prepare(repair=True)
        self.assertEqual(self.download.call_count, 2)
        self.assertEqual(tool.sha(archive), self.pins["archive_sha256"])

    def test_extracted_corruption_missing_binary_and_runtime_fail_closed(self):
        compiler, _ = self.prepare()
        for relative in ("bin/sdcc", "bin/sdcpp", "share/sdcc/lib/large/libsdcc.lib"):
            for mode in ("missing", "content", "permissions"):
                with self.subTest(relative=relative, mode=mode):
                    path = compiler.parents[1] / relative
                    if mode == "missing":
                        path.unlink()
                    elif mode == "content":
                        path.write_bytes(b"same advertised version, wrong content")
                    else:
                        path.chmod(0o600)
                    with self.assertRaises(ValueError):
                        self.prepare(offline=True)
                    self.prepare(offline=True, repair=True)
        self.assertEqual(self.download.call_count, 1)

    def test_manifest_rewrite_cannot_hide_modified_executable(self):
        compiler, _ = self.prepare()
        compiler.write_bytes(b"tampered")
        path = compiler.parents[1] / "MANIFEST.json"
        data = json.loads(path.read_bytes())
        data["files"]["bin/sdcc"]["sha256"] = tool.sha(compiler)
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "manifest mismatch"):
            self.prepare()

    def test_wrong_executable_pin_rejected_despite_valid_manifest(self):
        self.pins["executable_sha256"] = "a" * 64
        with self.assertRaisesRegex(ValueError, "executable mismatch"):
            self.prepare()
        self.probe.assert_not_called()

    def test_failed_capability_never_publishes_extraction(self):
        self.probe.side_effect = ValueError("unsupported schema")
        with self.assertRaisesRegex(ValueError, "unsupported schema"):
            self.prepare()
        self.assertFalse((self.cache / self.pins["tag"] / "toolchain").exists())
        self.probe.side_effect = None
        self.prepare(offline=True)

    def test_symlink_and_unaccounted_nested_manifest_rejected(self):
        compiler, _ = self.prepare()
        path = compiler.parents[1] / "share/MANIFEST.json"
        path.write_text("{}")
        with self.assertRaisesRegex(ValueError, "content/mode"):
            self.prepare()
        path.unlink()
        path.symlink_to("/usr/bin/sdcc")
        with self.assertRaisesRegex(ValueError, "Symlink"):
            self.prepare()

    def test_unsafe_archive_path_link_and_duplicate_rejected(self):
        name = self.pins["asset"].removesuffix(".tar.xz")
        for kind in ("escape", "symlink", "duplicate"):
            with self.subTest(kind=kind):
                self.cache = self.root / ("cache-" + kind)
                with tarfile.open(self.archive, "w:xz") as archive:
                    member = tarfile.TarInfo(name + ("/../escape" if kind == "escape" else "/item"))
                    member.mode = 0o644
                    if kind == "symlink":
                        member.type = tarfile.SYMTYPE
                        member.linkname = "/usr/bin/sdcc"
                    archive.addfile(member)
                    if kind == "duplicate":
                        archive.addfile(member)
                self.pins["archive_sha256"] = tool.sha(self.archive)
                with self.assertRaisesRegex(ValueError, "Unsafe"):
                    self.prepare()


class PinAndProbeTests(unittest.TestCase):
    def test_unpinned_latest_wrong_asset_and_schema_fail_closed(self):
        original = tool.load_pins()
        for key, value in (("tag", "latest"), ("tag", "main"), ("archive_sha256", ""),
                           ("ownership_schema", 2), ("ownership_schema", True),
                           ("asset", "different.tar.xz"), ("source_commit", "HEAD"),
                           ("version", True), ("url", "https://example.com/latest")):
            with self.subTest(key=key, value=value), \
                    patch.object(Path, "read_bytes", return_value=json.dumps(original | {key: value}).encode()), \
                    self.assertRaises(ValueError):
                tool.load_pins()

    def test_network_failure_and_false_download_contents(self):
        pins = tool.load_pins()
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / pins["asset"]
            with patch.object(tool.urllib.request, "urlopen", side_effect=urllib.error.URLError("offline")), \
                    self.assertRaisesRegex(ValueError, "download unavailable"):
                tool.download(pins, target)
            with patch.object(tool.urllib.request, "urlopen", return_value=io.BytesIO(b"false archive")), \
                    self.assertRaisesRegex(ValueError, "SHA256"):
                tool.download(pins, target)

    def test_real_probe_rejects_missing_schema_wrong_schema_and_changed_outputs(self):
        for mode in ("stock", "schema", "outputs"):
            def run(command, cwd, **kwargs):
                enabled = "--xdata-ownership" in command
                for suffix in ("asm", "rel", "adb", "lst", "sym", "ihx", "cdb", "mem"):
                    (cwd / ("capability." + suffix)).write_text(
                        str(enabled) if mode == "outputs" else "same")
                if enabled and mode != "stock":
                    (cwd / "capability.xdata.json").write_text(json.dumps(dict(
                        version=2 if mode == "schema" else 1, module="capability",
                        objects=[{"class": "LOCAL"}, {"class": "GLOBAL"}])))
                return subprocess.CompletedProcess(command, 0, "", "")
            with self.subTest(mode=mode), \
                    patch.object(tool.subprocess, "check_output", return_value="SDCC mcs51 4.2.0 #13081"), \
                    patch.object(tool.subprocess, "run", side_effect=run), self.assertRaises(ValueError):
                tool.capability(Path("/synthetic/sdcc"))


class RuntimeMapTests(unittest.TestCase):
    def table(self, parent):
        rows = [("mcs51.lib", n) for n in ("crtclear", "crtxinit", "crtxclear", "gptr_cmp", "crtstart")]
        rows += [("libsdcc.lib", "_gptrget")]
        data = b"                          [ object file ]\n\n"
        for library, member in rows:
            path = str(parent / library).encode()
            data += (path.ljust(42) if len(path) <= 40 else path + b"\n" + b" " * 42)
            data += b"[ " + member.encode() + b".rel ]\n"
        return data + b"\n\fASxxxx Linker V03.00 + NoICE + sdld,  page 373.\n\nUser Base Address Definitions\n"

    def test_exact_archive_bound_path_normalization_preserves_legacy_catalogs(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary) / "share/sdcc/lib/large"
            parent.mkdir(parents=True)
            wanted = {}
            for name in ("mcs51.lib", "libsdcc.lib"):
                (parent / name).write_bytes(name.encode())
                wanted[name] = tool.sha(parent / name)
            data = self.table(parent)
            with patch.object(tool, "load_pins", return_value={"runtime_archives": wanted}):
                for overlay in (False, True):
                    legacy = Path("/usr/bin/../share/sdcc/lib/large" if overlay else "/usr/share/sdcc/lib/large")
                    self.assertEqual(runtime_map_identity(data, ownership=True, overlay=overlay), self.table(legacy))
                (parent / "mcs51.lib").write_bytes(b"wrong")
                with self.assertRaisesRegex(ValueError, "archive identity"):
                    runtime_map_identity(data, ownership=True, overlay=True)

    def test_member_order_format_extra_rows_and_mixed_roots_fail_closed(self):
        data = self.table(Path("/usr/share/sdcc/lib/large"))
        self.assertEqual(runtime_map_identity(data, ownership=False, overlay=False), data)
        for changed in (data.replace(b"crtclear", b"crtxinit"), data.replace(b"mcs51.lib", b"foreign.lib", 1),
                        data.replace(b"[ crtclear.rel ]", b"[  crtclear.rel ]"),
                        data.replace(b"\n\fASxxxx", b"unexpected\n\n\fASxxxx"),
                        data.replace(b"/usr/share", b"/other/root", 1)):
            with self.assertRaises(ValueError):
                runtime_map_identity(changed, ownership=True, overlay=True)


if __name__ == "__main__":
    unittest.main()
