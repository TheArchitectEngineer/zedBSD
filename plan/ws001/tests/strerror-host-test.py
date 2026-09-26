#!/usr/bin/env python3
"""ws001: checks the C library's strerror and strerror_r on the host.

src/libc/string.c is compiled for the host against the zedBSD headers,
with strerror and strerror_r renamed so that they do not meet the host's,
into a shared object that ctypes loads.  Every error number that
include/uapi/errno.h defines must get a description of its own that is not
"Unknown error"; a number that is not an error number must get "Unknown
error N" and, from strerror_r, EINVAL; a small buffer must get ERANGE and a
terminated prefix.

  python3 plan/ws001/tests/strerror-host-test.py [STRING_C]

(STRING_C, default src/libc/string.c, lets an older copy be checked.)
"""

import ctypes
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

# heap_strdup_active is what strdup calls; the test never reaches it.
STUB = "char *heap_strdup_active(const char *s) { (void)s; return 0; }\n"


def build(work: Path, source: Path) -> Path:
	"""Compiles string.c and the stub into a shared object."""
	stub = work / "stub.c"
	stub.write_text(STUB)
	library = work / "libzedstring.so"
	subprocess.run(
	    ["cc", "-shared", "-fPIC", "-O1", "-nostdinc",
	     "-isystem", str(ROOT / "include/libc"), "-I", str(ROOT / "include"),
	     "-I", str(ROOT), "-DKERN_UAPI_NATIVE",
	     "-Dstrerror=zedbsd_strerror", "-Dstrerror_r=zedbsd_strerror_r",
	     "-Dmemcpy=zedbsd_memcpy", "-Dmemmove=zedbsd_memmove",
	     "-Dmemset=zedbsd_memset", "-Dstrlen=zedbsd_strlen",
	     str(source), str(stub), "-o", str(library)],
	    check=True)
	return library


def error_numbers() -> dict[str, int]:
	"""Reads every numeric error number definition of <uapi/errno.h>."""
	numbers = {}
	text = (ROOT / "include/uapi/errno.h").read_text()
	for match in re.finditer(r"^#define (E[A-Z0-9]+) (\d+)\s*$", text, re.M):
		numbers[match.group(1)] = int(match.group(2))
	return numbers


def main() -> int:
	with tempfile.TemporaryDirectory(prefix="ws001-strerror-") as work:
		source = ROOT / "src/libc/string.c"
		if len(sys.argv) > 1:
			source = Path(sys.argv[1]).resolve()
		library = ctypes.CDLL(str(build(Path(work), source)))
	strerror = library.zedbsd_strerror
	strerror.restype = ctypes.c_char_p
	strerror.argtypes = [ctypes.c_int]
	strerror_r = library.zedbsd_strerror_r
	strerror_r.restype = ctypes.c_int
	strerror_r.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_size_t]

	numbers = error_numbers()
	failures = []
	seen: dict[bytes, str] = {}
	einval = numbers["EINVAL"]
	erange = numbers["ERANGE"]

	# Every error number has a description of its own.
	for name, number in sorted(numbers.items(), key=lambda item: item[1]):
		text = strerror(number)
		if text.startswith(b"Unknown error"):
			failures.append("%s (%d): %r" % (name, number, text))
		if text in seen:
			failures.append("%s shares %r with %s" % (name, text, seen[text]))
		seen[text] = name
		buffer = ctypes.create_string_buffer(256)
		result = strerror_r(number, buffer, 256)
		if result != 0 or buffer.value != text:
			failures.append("strerror_r %s: %d %r" % (name, result, buffer.value))

	# A number that is not an error number is described with its value.
	for number in (0, 9999, -5):
		if number in numbers.values():
			continue
		text = strerror(number)
		if text != b"Unknown error %d" % number:
			failures.append("strerror(%d): %r" % (number, text))
		buffer = ctypes.create_string_buffer(64)
		result = strerror_r(number, buffer, 64)
		if result != einval or buffer.value != text:
			failures.append("strerror_r(%d): %d %r" % (number, result, buffer.value))

	# A buffer too small is filled and terminated, and ERANGE is returned.
	buffer = ctypes.create_string_buffer(b"x" * 8, 8)
	result = strerror_r(numbers["ENOENT"], buffer, 8)
	if result != erange or buffer.value != strerror(numbers["ENOENT"])[:7]:
		failures.append("strerror_r small buffer: %d %r" % (result, buffer.value))

	for failure in failures:
		print("FAIL", failure)
	print("error numbers %d, failures %d" % (len(numbers), len(failures)))
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
