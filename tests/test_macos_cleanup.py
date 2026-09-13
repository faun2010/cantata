"""Exercise the destructive cleanup command only in disposable Git fixtures."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class MacOSCleanupTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.project = Path(self.temporary.name) / "project with spaces"
        (self.project / "scripts").mkdir(parents=True)
        self.script = self.project / "scripts/clean-macos-local.py"
        shutil.copyfile(Path(__file__).resolve().parents[1] / "scripts/clean-macos-local.py", self.script)
        subprocess.run(["git", "init", "-q", str(self.project)], check=True)
        for directory in ("build-macos", "build-delivery", "build-tests-one", "build-deps", "dist", "tmp", "output"):
            (self.project / directory).mkdir()
            (self.project / directory / "keep-or-remove.txt").write_text(directory)

    def run_cleanup(self, *args):
        return subprocess.run([sys.executable, str(self.script), *args], capture_output=True, text=True)

    def test_preview_and_apply_preserve_deliverables_and_sdk(self):
        self.assertEqual(self.run_cleanup().returncode, 0)
        self.assertTrue((self.project / "build-macos/keep-or-remove.txt").exists())
        self.assertEqual(self.run_cleanup("--apply").returncode, 0)
        for directory in ("build-macos", "build-delivery", "build-tests-one"):
            self.assertFalse((self.project / directory).exists())
        for directory in ("build-deps", "dist", "tmp", "output"):
            self.assertTrue((self.project / directory / "keep-or-remove.txt").exists())
        self.assertEqual(self.run_cleanup("--apply", "--include-sdk").returncode, 0)
        self.assertFalse((self.project / "build-deps").exists())
        self.assertTrue((self.project / "dist/keep-or-remove.txt").exists())

    def test_symlink_target_aborts_before_deleting_anything(self):
        link = self.project / "build-tests-outside"
        link.symlink_to(self.project / "dist", target_is_directory=True)
        self.assertNotEqual(self.run_cleanup("--apply").returncode, 0)
        self.assertTrue((self.project / "dist/keep-or-remove.txt").exists())
        self.assertTrue((self.project / "build-macos/keep-or-remove.txt").exists())

    def test_tracked_files_abort_before_deleting_anything(self):
        subprocess.run(["git", "-C", str(self.project), "add", "build-delivery/keep-or-remove.txt"], check=True)
        self.assertNotEqual(self.run_cleanup("--apply").returncode, 0)
        self.assertTrue((self.project / "build-macos/keep-or-remove.txt").exists())

    def test_nested_checkout_is_preserved(self):
        (self.project / "build-delivery/.git").write_text("gitdir: /some/other/checkout\n")
        self.assertNotEqual(self.run_cleanup("--apply").returncode, 0)
        self.assertTrue((self.project / "build-macos/keep-or-remove.txt").exists())


if __name__ == "__main__":
    unittest.main()
