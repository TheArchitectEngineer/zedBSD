#!/usr/bin/env python3
"""Run proposal host quiescence semantics locally with existing Mesa build helpers."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def execute(command, log):
    started = time.monotonic()
    with log.open("w") as output:
        try:
            result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=120)
            return {"exit": result.returncode, "pass": result.returncode == 0,
                    "seconds": time.monotonic() - started, "log": str(log)}
        except subprocess.TimeoutExpired:
            return {"exit": None, "pass": False, "timeout": True,
                    "seconds": time.monotonic() - started, "log": str(log)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--helpers", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source, helpers, output = args.source.resolve(), args.helpers.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    includes = [source] + [source / item for item in ("src/venus", "src/proxy", "src", "src/gallium/include",
                "src/gallium/auxiliary", "src/gallium/auxiliary/util", "src/mesa", "src/mesa/compat",
                "src/mesa/pipe", "src/mesa/util")]
    flags = ["-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
             "-Wno-unused-parameter", "-ffunction-sections", "-fdata-sections", "-D_FILE_OFFSET_BITS=64",
             "-DHAVE_CONFIG_H=1", "-imacros", str(helpers / "config.h"), "-I" + str(helpers)]
    flags += ["-I" + str(path) for path in includes]
    results = []
    for case in ("context", "proxy"):
        for profile in ("ordinary", "sanitized"):
            executable = output / (case + "-" + profile)
            command = ["cc", *flags, str(Path(__file__).with_name(case + ".c")),
                       str(helpers / "libmesa.a"), "-Wl,--gc-sections", "-pthread", "-lm", "-o", str(executable)]
            if case == "proxy":
                command += [str(source / "src/proxy/proxy_socket.c"), str(source / "src/virgl_util.c")]
            if profile == "sanitized":
                command += ["-O1", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            compiled = execute(command, executable.with_suffix(".compile.log"))
            result = {"case": case, "profile": profile, "compile": compiled, "pass": False}
            if compiled["pass"]:
                result["run"] = execute([str(executable)], executable.with_suffix(".run.log"))
                result["pass"] = result["run"]["pass"]
            results.append(result)
            print(json.dumps(result), flush=True)
    hashes = {str(path.relative_to(source)): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in (source / "src/venus/vkr_context.c", source / "src/venus/vkr_context.h",
                           source / "src/proxy/proxy_context.c")}
    report = {"scope": "proposed isolated quiescence source, native idle mocks and private CPU0 socketpair only",
              "source_sha256": hashes, "results": results, "pass": all(item["pass"] for item in results)}
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
