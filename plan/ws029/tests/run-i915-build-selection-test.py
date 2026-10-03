#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Check actual make-expanded GPU ownership for Venus and i915 selections on every platform list."""

import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[3]
TARGETS = (
    ("amd64", "amd64", "pcat", "AMD64_KERNEL_OBJS"),
    ("rpi4", "arm64", "rpi4", "ARM64_KERNEL_OBJS"),
    ("i386", "i386", "pcat", "KERN_OBJS"),
    ("pc98", "i386", "pc98", "KERN_OBJS"),
    ("sun4u", "sparcv9", "sun4u", "SPARCV9_KERNEL_OBJS"),
    ("x68k", "m68k", "x68k", "X68K_KERNEL_OBJS"),
)
# The i915 driver's sources live below this directory; objects are named by their path under it.
I915_PREFIX = "src/drivers/gpu/i915/"
# Objects every i915 kernel links: the PCI entry, the device, the core, the executor, the compiler, the display.
I915_REQUIRED = ("i915.o", "device.o", "ppgtt.o", "command.o", "render/vulkan.o", "compiler/spirv.o", "display/display.o")


def object_name(path):
    """Returns an i915 object's path below src/drivers/gpu/i915/."""
    index = path.index(I915_PREFIX)
    return path[index + len(I915_PREFIX):]


def main():
    with tempfile.TemporaryDirectory(prefix="zedbsd-i915-build-") as work:
        observer = pathlib.Path(work) / "observe.mk"
        observer.write_text(
            ".PHONY: q314-gpu-selection\n"
            "q314-gpu-selection:\n"
            "\t@printf '%s\\n' '$(KERN_GPU_SOURCES)' "
            "'$(filter %/gpu.o %/gpu-fence.o,$($(Q314_OBJECT_LIST)))' "
            "'$($(Q314_OBJECT_LIST))' "
            "'$(filter %/gpu/venus/venus.o %/gpu/venus/transport.o %/gpu/venus/display.o %/gpu/venus/share.o,$($(Q314_OBJECT_LIST)))' "
            "'$(AMD64_I915_SOURCES)'\n"
        )
        for platform, architecture, board, objects in TARGETS:
            for venus in ("n", "y"):
                for i915 in ("n", "y"):
                    result = subprocess.run(
                        [
                            "make", "--no-print-directory", "-s", "-f", "Makefile",
                            "-f", str(observer), "ZEDBSD_CONFIG=/dev/null",
                            f"ZEDBSD_PLATFORM={platform}",
                            f"ZEDBSD_ARCHITECTURE={architecture}",
                            f"ZEDBSD_BOARD={board}",
                            f"CONFIG_DRIVER_PCI_VENUS={venus}",
                            f"CONFIG_DRIVER_PCI_I915={i915}",
                            "I915_TESTS=n",
                            f"Q314_OBJECT_LIST={objects}", "q314-gpu-selection",
                        ], cwd=ROOT, text=True, capture_output=True, timeout=30,
                        check=True,
                    )
                    lines = result.stdout.split("\n")
                    assert len(lines) == 6 and lines[5] == "", (platform, venus, i915, result.stdout)
                    sources, gpu_objects, kernel_objects, venus_objects, i915_sources = lines[:5]
                    # make's filter takes one wildcard, so the i915 objects are picked out here.
                    i915_objects = [p for p in kernel_objects.split() if "/" + I915_PREFIX in p]
                    if venus == "n" and i915 == "n":
                        assert not sources and not gpu_objects, (platform, lines)
                    else:
                        assert sources.split() == [
                            "src/drivers/gpu/gpu.c", "src/drivers/gpu/gpu-fence.c"
                        ], (platform, sources)
                        assert len(gpu_objects.split()) == 2, (platform, gpu_objects)
                    # Only the amd64 kernel links either backend; other lists never gain them.
                    names = sorted(object_name(p) for p in i915_objects)
                    if platform == "amd64" and i915 == "y":
                        # Every i915 source of the build list is linked as one object, and nothing else is.
                        expected = sorted(object_name(p)[:-2] + ".o" for p in i915_sources.split())
                        assert names == expected, (platform, names, expected)
                        missing = [name for name in I915_REQUIRED if name not in names]
                        assert not missing, (platform, missing)
                        # The production kernel links no test code.
                        tests = [name for name in names if name.startswith("tests/")]
                        assert not tests, (platform, tests)
                    else:
                        assert not names, (platform, i915_objects)
                    if platform == "amd64" and venus == "y":
                        assert len(venus_objects.split()) == 4, (platform, venus_objects)
                    else:
                        assert not venus_objects, (platform, venus_objects)
                    print(f"GPU build selection: {platform} Venus={venus} i915={i915} PASS")
    print("GPU framework follows either backend; i915 objects appear only on amd64 with CONFIG_DRIVER_PCI_I915=y PASS")


if __name__ == "__main__":
    main()
