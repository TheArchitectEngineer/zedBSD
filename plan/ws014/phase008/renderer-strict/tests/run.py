#!/usr/bin/env python3
"""Build bounded semantic fixtures with the isolated renderer's real configuration."""

import argparse
import json
from pathlib import Path
import shlex
import subprocess
import time


def compiler_flags(entry):
    words = shlex.split(entry["command"])
    flags = []
    skip = False
    for word in words[1:]:
        if skip:
            skip = False
            continue
        if word in ("-o", "-MF", "-MQ", "-MT"):
            skip = True
            continue
        if word in ("-c", "-MD", "-MMD", "-DNDEBUG") or word == entry["file"]:
            continue
        flags.append(word)
    return words[0], flags


def execute(command, directory, log):
    started = time.monotonic()
    try:
        with log.open("w") as stream:
            process = subprocess.run(command, cwd=directory, stdout=stream,
                                     stderr=subprocess.STDOUT, timeout=120)
        return {"exit": process.returncode, "seconds": time.monotonic() - started,
                "log": str(log), "pass": process.returncode == 0}
    except subprocess.TimeoutExpired:
        return {"exit": None, "seconds": time.monotonic() - started,
                "log": str(log), "pass": False, "timeout": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dependency-root", type=Path, required=True)
    args = parser.parse_args()
    root = args.dependency_root.resolve()
    entries = json.loads((root / "build/compile_commands.json").read_text())
    evidence = root / "fixture-evidence"
    evidence.mkdir(exist_ok=True)
    results = []
    for case, unit in (("queue", "vkr_queue.c"), ("proxy", "proxy_context.c")):
        entry = next(item for item in entries if item["file"].endswith("/" + unit))
        compiler, flags = compiler_flags(entry)
        for profile in ("ordinary", "sanitized"):
            output = evidence / (case + "-" + profile)
            command = [compiler, *flags, "-UNDEBUG", "-ffunction-sections", "-fdata-sections",
                       "-I" + str(root / "source/src/proxy"),
                       str(Path(__file__).with_name(case + ".c")),
                       str(root / "build/src/mesa/libmesa.a"),
                       "-Wl,--gc-sections", "-pthread", "-lm", "-o", str(output)]
            if case == "proxy":
                command += [str(root / "source/src/proxy/proxy_socket.c"),
                            str(root / "source/src/virgl_util.c")]
            if profile == "sanitized":
                command += ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            stem = case + "-" + profile
            compiled = execute(command, entry["directory"], evidence / (stem + "-compile.log"))
            result = {"case": case, "profile": profile, "compile": compiled, "pass": False}
            if compiled["pass"]:
                result["run"] = execute([str(output)], entry["directory"], evidence / (stem + "-run.log"))
                result["pass"] = result["run"]["pass"]
            results.append(result)
            print(json.dumps(result), flush=True)
    report = {"scope": "actual renderer source; scripted native Vulkan only",
              "results": results, "pass": all(item["pass"] for item in results)}
    (evidence / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
