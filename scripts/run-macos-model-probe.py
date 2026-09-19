#!/usr/bin/env python3
"""Link a model probe against the native app's existing production objects."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--build-dir", type=Path, default=root / "build-macos")
    options = parser.parse_args()
    source = options.source.resolve(strict=True)
    build = options.build_dir.resolve(strict=True)
    commands = subprocess.check_output(["ninja", "-t", "commands", "cantata"], cwd=build, text=True).splitlines()
    main_object = "CMakeFiles/cantata.dir/gui/main.cpp.o"
    compile_line = next(line for line in commands if main_object in line and " -c " in line and line.endswith("/gui/main.cpp"))
    output = "probe-" + source.stem
    compile_args = [output + ".o" if arg == main_object else
                    output + ".o.d" if arg == main_object + ".d" else
                    str(source) if arg.endswith("/gui/main.cpp") else arg
                    for arg in shlex.split(compile_line)]
    link = shlex.split(commands[-1])
    if link[:2] != [":", "&&"] or link[-2:] != ["&&", ":"] or main_object not in link:
        raise RuntimeError("Expected the Ninja executable link command; build the app first.")
    link = [output + ".o" if arg == main_object else arg for arg in link[2:-2]]
    link[link.index("-o") + 1] = output
    with (build / (output + "-build.log")).open("w") as log:
        subprocess.run(compile_args, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    environment = os.environ.copy()
    environment.setdefault("QT_QPA_PLATFORM", "offscreen")
    result = subprocess.run([str(build / output)], cwd=build, env=environment, capture_output=True, text=True)
    report = result.stdout + result.stderr
    (build / (output + "-result.txt")).write_text(report)
    print(report, end="")
    print(f"{source.name}: {'PASS' if result.returncode == 0 else 'FAIL'}")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
