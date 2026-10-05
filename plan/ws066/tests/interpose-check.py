#!/usr/bin/env python3
"""ws066-p001: the functions of libc.so that another object of a guest root also defines.

With -Bsymbolic-functions, libc.so's own calls go to its own functions even
when the program or another library defines a function of the same name
(interposition).  This lists, for every ELF object under ROOT that has a
dynamic symbol table, the global functions it defines that libc.so defines
too, so that the candidate's risk is known before it is chosen.

    interpose-check.py ROOT

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
reloc_model = __import__("reloc-model")

STT_FUNC = 2
STT_GNU_IFUNC = 10


def defined_functions(image):
    names = set()
    for index in range(1, image.symbol_count):
        name, binding, kind, shndx = image.symbol(index)
        if shndx != 0 and binding != 0 and kind in (STT_FUNC, STT_GNU_IFUNC):
            names.add(name)
    return names


def main():
    root = sys.argv[1]
    libc_path = os.path.realpath(os.path.join(root, "lib/libc.so"))
    libc = defined_functions(reloc_model.Image(libc_path))
    found = 0
    for directory, _subdirs, files in os.walk(root):
        for file_name in sorted(files):
            path = os.path.join(directory, file_name)
            if os.path.islink(path) or os.path.realpath(path) == libc_path:
                continue
            try:
                with open(path, "rb") as stream:
                    if stream.read(4) != b"\x7fELF":
                        continue
                image = reloc_model.Image(path)
            except (ValueError, KeyError, TypeError, OSError, IndexError):
                continue
            if not hasattr(image, "symbol_count"):
                continue
            common = sorted(defined_functions(image) & libc)
            if common:
                found += 1
                shown = b" ".join(common[:12]).decode()
                more = f" (+{len(common) - 12})" if len(common) > 12 else ""
                print(f"{os.path.relpath(path, root)}: {len(common)}: {shown}{more}")
    print(f"interpose-check: {found} objects define functions libc.so defines")


if __name__ == "__main__":
    main()
