#!/usr/bin/env python3
"""Compares the build time of the tests in two Ninja build directories, from their .ninja_log (docs/WEBFRONT-1.2.md).

    ninja_log_summary.py <label>=<build directory> <label>=<build directory>

Prints a Markdown table. "CPU" sums the duration of each build step (the steps run in parallel, so it is the work done,
not the time waited); "wall" is the time from the first step to the last one.
"""
import re
import sys
from pathlib import Path

GROUPS = [
    ("Test sources", re.compile(r"tests/CMakeFiles/siliconforge_(bridge_)?tests\.dir/.*\.(o|obj)$")),
    ("Test executables (link)", re.compile(r"tests/siliconforge_(bridge_)?tests(\.exe)?$")),
    ("Catch2 library", re.compile(r"_deps/catch2-build/")),
]


def read(build):
    steps = {}
    for line in (Path(build) / ".ninja_log").read_text().splitlines():
        if line.startswith("#"):
            continue
        start, end, _, output, _ = line.split("\t")
        steps[output.replace("\\", "/")] = (int(start), int(end))
    return steps


def summarize(build):
    steps = read(build)
    result = {"count": {}, "cpu": {}}
    for name, pattern in GROUPS:
        durations = [end - start for output, (start, end) in steps.items() if pattern.search(output)]
        result["count"][name] = len(durations)
        result["cpu"][name] = sum(durations) / 1000
    result["cpu"]["Whole build"] = sum(end - start for start, end in steps.values()) / 1000
    result["wall"] = (max(end for _, end in steps.values()) - min(start for start, _ in steps.values())) / 1000
    return result


def main(arguments):
    (first, first_build), (second, second_build) = (argument.split("=", 1) for argument in arguments)
    a, b = summarize(first_build), summarize(second_build)
    print(f"| Step | {first} steps | {first} CPU (s) | {second} steps | {second} CPU (s) | Change |")
    print("| --- | ---: | ---: | ---: | ---: | ---: |")
    for name in [*(name for name, _ in GROUPS), "Whole build"]:
        before, after = a["cpu"][name], b["cpu"][name]
        change = f"{(after - before) / before * 100:+.0f} %" if before else "n/a"
        print(f"| {name} | {a['count'].get(name, '')} | {before:.1f} | {b['count'].get(name, '')} | {after:.1f} | {change} |")
    change = f"{(b['wall'] - a['wall']) / a['wall'] * 100:+.0f} %"
    print(f"| Whole build, wall clock | | {a['wall']:.1f} | | {b['wall']:.1f} | {change} |")


main(sys.argv[1:])
