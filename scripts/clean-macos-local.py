#!/usr/bin/env python3
"""Preview or explicitly remove this project's default macOS build artifacts."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def cleanup_targets(project: Path, include_sdk: bool) -> list[Path]:
    names = ["build-macos", "build-delivery", "aqtinstall.log"]
    names.extend(sorted(path.name for path in project.glob("build-tests-*")))
    if include_sdk:
        names.append("build-deps")
    return [project / name for name in names if os.path.lexists(project / name)]


def validate_targets(project: Path, targets: list[Path]) -> None:
    for path in targets:
        if path.is_symlink() or path.parent != project:
            raise ValueError(f"Refusing symlink or non-project path: {path}")
        tracked = subprocess.check_output(
            ["git", "-C", str(project), "ls-files", "--", path.name], text=True
        )
        if tracked:
            raise ValueError(f"Refusing directory containing tracked files: {path}")
        if path.is_dir():
            for current, dirs, files in os.walk(path, followlinks=False):
                if os.path.ismount(current) or any(
                    os.path.ismount(Path(current) / name) for name in dirs
                ):
                    raise ValueError(f"Detach the mounted volume before cleanup: {current}")
                if ".git" in dirs or ".git" in files:
                    raise ValueError(f"Refusing directory containing a Git checkout: {current}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true", help="delete the listed artifacts; default is preview only")
    parser.add_argument("--include-sdk", action="store_true", help="also delete build-deps; a future build will need Qt restored")
    options = parser.parse_args()
    project = Path(__file__).resolve().parent.parent
    targets = cleanup_targets(project, options.include_sdk)
    if not targets:
        print("No default macOS build artifacts to clean.")
        return 0
    validate_targets(project, targets)
    print("Delete:" if options.apply else "Preview (nothing will be deleted):", flush=True)
    subprocess.run(["du", "-sh", *map(str, targets)], check=True)
    print("Includes previous App/DMG backups stored inside the build directories.", flush=True)
    print("Preserved: dist/, source, Git history, tmp/, output/ and app settings/translation caches.", flush=True)
    if not options.include_sdk:
        print("Preserved: build-deps/ (private Qt SDK for the next build).", flush=True)
    if not options.apply:
        print("Run again with --apply to delete these artifacts.")
        return 0
    # Validate the complete list before deleting anything.
    for path in targets:
        if path.is_dir():
            shutil.rmtree(path)
        else:
            path.unlink()
    print("Cleanup complete.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
