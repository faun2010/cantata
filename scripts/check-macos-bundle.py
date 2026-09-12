#!/usr/bin/env python3
"""Reject macOS bundles with unbundled non-system dylibs or build rpaths."""
from pathlib import Path
import subprocess
import sys

bundle = Path(sys.argv[1]).resolve()
magic = {b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf"}
errors = []
count = 0
for path in bundle.rglob("*"):
    if not path.is_file() or path.is_symlink():
        continue
    with path.open("rb") as source:
        if source.read(4) not in magic:
            continue
    count += 1
    libraries = subprocess.check_output(["otool", "-L", str(path)], text=True)
    for line in libraries.splitlines():
        if " (compatibility version " not in line:
            continue
        dependency = line.strip().split(" (compatibility version ", 1)[0]
        if dependency.startswith(("/System/Library/", "/usr/lib/")):
            continue
        if dependency.startswith("@rpath/"):
            target = bundle / "Contents/Frameworks" / dependency[len("@rpath/"):]
        elif dependency.startswith("@executable_path/"):
            target = bundle / "Contents/MacOS" / dependency[len("@executable_path/"):]
        elif dependency.startswith("@loader_path/"):
            target = path.parent / dependency[len("@loader_path/"):]
        else:
            errors.append(f"{path.relative_to(bundle)}: external dependency {dependency}")
            continue
        if not target.resolve().is_relative_to(bundle):
            errors.append(f"{path.relative_to(bundle)}: dependency escapes bundle: {dependency}")
        elif not target.exists():
            errors.append(f"{path.relative_to(bundle)}: missing {dependency}")
    commands = subprocess.check_output(["otool", "-l", str(path)], text=True).splitlines()
    for index, line in enumerate(commands):
        if line.strip() == "cmd LC_RPATH":
            rpath = commands[index + 2].strip().removeprefix("path ").split(" (offset ", 1)[0]
            if rpath.startswith("/") and not rpath.startswith(("/System/Library/", "/usr/lib/")):
                errors.append(f"{path.relative_to(bundle)}: external rpath {rpath}")
if not count:
    errors.append("No Mach-O binaries found")
if errors:
    sys.exit("\n".join(errors))
print(f"Verified {count} Mach-O binaries: dependencies are bundled or provided by macOS.")
