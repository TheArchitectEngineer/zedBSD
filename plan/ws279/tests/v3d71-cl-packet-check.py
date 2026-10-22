#!/usr/bin/env python3
"""Compare the V3D 7.1 self-test streams with packets built from the pinned Mesa XML (ws279-p003).

The oracle reads the 7.1 packet definitions (min_ver/max_ver bounding 71)
rather than the C templates.  It needs WS141's ignored Mesa snapshot.
"""

import hashlib
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

PINNED_SHA256 = "b13f995b1a4b606a1adcb2c048ff9bca067106be14e22ce1ee86e01055094e1b"
VERSION = 71


def packet_image(root, name, values):
    packets = [node for node in root.findall("packet")
               if node.get("name") == name
               and int(node.get("min_ver", "0")) <= VERSION
               and int(node.get("max_ver", "999")) >= VERSION]
    if len(packets) != 1:
        raise ValueError(f"expected one 7.1 definition for {name}, found {len(packets)}")
    packet = packets[0]
    payload = 0
    bits = 0
    names = set()
    for field in packet.findall("field"):
        field_name = field.get("name")
        names.add(field_name)
        start = int(field.get("start"))
        size = int(field.get("size"))
        bits = max(bits, start + size)
        value = values.get(field_name, int(field.get("default", "0")))
        if field.get("minus_one") == "true":
            value -= 1
        if field.get("type") == "address":
            alignment_bits = start % 8
            if value & ((1 << alignment_bits) - 1):
                raise ValueError(f"unaligned {name}/{field_name}")
            value >>= alignment_bits
        if not 0 <= value < (1 << size):
            raise ValueError(f"unrepresentable {name}/{field_name}: {value}")
        payload |= value << start
    if values.keys() - names:
        raise ValueError(f"unknown fields in {name}: {values.keys() - names}")
    return bytes([int(packet.get("code"))]) + payload.to_bytes((bits + 7) // 8, "little")


def supertile():
    return ("Multicore Rendering Supertile Cfg", {"Number of Bin Tile Lists": 1,
            "Total Frame Height in Tiles": 1, "Total Frame Width in Tiles": 1,
            "Total Frame Height in Supertiles": 1, "Total Frame Width in Supertiles": 1,
            "Supertile Height in Tiles": 1, "Supertile Width in Tiles": 1})


def streams(width, height, clear):
    tile_size = {"Log2 Tile Width": 3, "Log2 Tile Height": 3}
    common = {"Image Width (pixels)": width, "Image Height (pixels)": height,
              "Number of Render Targets": 1, "Early-Z disable": 1}
    common.update(tile_size)
    # A 64-pixel 32-bit row covers 32 128-bit units over two rows; the noop's target is unused (stride 1).
    target = {"Internal Type and Clamping": 8, "Stride": 32 if clear else 1}
    if clear:
        target["Clear Color low bits"] = 0xff317ce0
    render = [
        ("Tile Rendering Mode Cfg (Common)", common),
        ("Tile Rendering Mode Cfg (Render Target Part1)", target),
        ("Tile Rendering Mode Cfg (ZS Clear Values)", {"Z Clear Value": 0x3f800000}),
        ("Tile List Initial Block Size", {"Use auto-chained tile lists": 1}),
        ("Multicore Rendering Tile List Set Base", {"address": 0x40000}),
        supertile(),
    ]
    if clear:
        for initial_tile in range(2):
            render += [("Tile Coordinates", {}), ("End of Loads", {}),
                       ("Store Tile Buffer General", {"Buffer to Store": 8})]
            if initial_tile == 0:
                render.append(("Clear Render Targets", {}))
            render.append(("End of Tile Marker", {}))
        render.append(("Flush VCD cache", {}))
    render += [("Start Address of Generic Tile List", {"start": 0x30000, "end": 0x30013}),
               ("Supertile Coordinates", {}), ("End of rendering", {})]
    store = {"Buffer to Store": 8}
    if clear:
        store = {"Buffer to Store": 0, "Output Image Format": 27,
                 "Height in UB or Stride": 256, "Address": 0x100000}
    binning = {"Width (in pixels)": width, "Height (in pixels)": height}
    binning.update(tile_size)
    return {
        "bin": [("Number of Layers", {"Number of Layers": 1}),
                ("Tile Binning Mode Cfg", binning),
                ("Flush VCD cache", {}), ("Start Tile Binning", {}), ("Flush", {})],
        "render": render,
        "tile": [("Tile Coordinates Implicit", {}), ("End of Loads", {}),
                 ("Branch to Implicit Tile List", {}),
                 ("Store Tile Buffer General", store),
                 ("End of Tile Marker", {}), ("Return from sub-list", {})],
    }


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: v3d71-cl-packet-check.py PINNED_XML HOST_OUTPUT")
    xml = Path(sys.argv[1]).read_bytes()
    if hashlib.sha256(xml).hexdigest() != PINNED_SHA256:
        raise SystemExit("packet XML does not match WS141's audited snapshot")
    root = ET.fromstring(xml)
    jobs = {"noop": streams(1, 1, False), "clear": streams(64, 64, True)}
    observed = {}
    for line in Path(sys.argv[2]).read_text().splitlines():
        for job in jobs:
            if line.startswith(job + "-image "):
                _, name, encoded = line.split()
                observed[(job, name)] = bytes.fromhex(encoded)
    for job, expected_streams in jobs.items():
        for name, packets in expected_streams.items():
            expected = b"".join(packet_image(root, packet, values) for packet, values in packets)
            got = observed.get((job, name))
            if got != expected:
                raise SystemExit(f"{job}/{name} differs from the 7.1 XML: expected {expected.hex()}, got {got.hex() if got else None}")
    print("v3d71-cl-packet-check: PASS (7.1 XML, noop and clear bin/render/tile)")


if __name__ == "__main__":
    main()
