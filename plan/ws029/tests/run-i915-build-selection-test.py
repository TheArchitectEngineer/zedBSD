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
I915_OBJECTS = ("i915.o", "uncore.o", "ggtt.o", "ppgtt.o", "gem.o", "irq.o", "engine.o", "lrc.o", "request.o")


def main():
    with tempfile.TemporaryDirectory(prefix="zedbsd-i915-build-") as work:
        observer = pathlib.Path(work) / "observe.mk"
        observer.write_text(
            ".PHONY: q314-gpu-selection\n"
            "q314-gpu-selection:\n"
            "\t@printf '%s\\n' '$(KERN_GPU_SOURCES)' "
            "'$(filter %/gpu.o %/gpu-fence.o,$($(Q314_OBJECT_LIST)))' "
            "'$(filter %/gpu/i915/i915.o %/gpu/i915/uncore.o %/gpu/i915/ggtt.o %/gpu/i915/ppgtt.o %/gpu/i915/gem.o %/gpu/i915/irq.o %/gpu/i915/engine.o %/gpu/i915/lrc.o %/gpu/i915/request.o %/gpu/i915/selftest.o,$($(Q314_OBJECT_LIST)))' "
            "'$(filter %/gpu/venus/venus.o %/gpu/venus/transport.o %/gpu/venus/display.o %/gpu/venus/share.o,$($(Q314_OBJECT_LIST)))'\n"
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
                            f"Q314_OBJECT_LIST={objects}", "q314-gpu-selection",
                        ], cwd=ROOT, text=True, capture_output=True, timeout=30,
                        check=True,
                    )
                    lines = result.stdout.splitlines()
                    assert len(lines) == 4, (platform, venus, i915, result.stdout)
                    sources, gpu_objects, i915_objects, venus_objects = lines
                    if venus == "n" and i915 == "n":
                        assert not sources and not gpu_objects, (platform, lines)
                    else:
                        assert sources.split() == [
                            "src/drivers/gpu/gpu.c", "src/drivers/gpu/gpu-fence.c"
                        ], (platform, sources)
                        assert len(gpu_objects.split()) == 2, (platform, gpu_objects)
                    # Only the amd64 kernel links either backend; other lists never gain them.
                    names = sorted(pathlib.Path(p).name for p in i915_objects.split())
                    if platform == "amd64" and i915 == "y":
                        assert names == sorted(I915_OBJECTS), (platform, i915_objects)
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
