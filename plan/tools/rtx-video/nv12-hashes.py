#!/usr/bin/env python3
# The software decoder's reference for media-probe --video-hash: ffmpeg decodes the clip's first video stream to
# 8-bit 4:2:0 in display order, and each frame's visible NV12 bytes (the luma rows, then the chroma rows with Cb and
# Cr interleaved) are hashed as media-probe hashes a picture.  Prints "TIME_US SHA256" lines, one a frame.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: python3 -I plan/tools/rtx-video/nv12-hashes.py CLIP.mp4 > CLIP.ref
import hashlib
import json
import subprocess
import sys


def main():
    clip = sys.argv[1]
    probe = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
         "-show_entries", "frame=best_effort_timestamp_time", "-of", "json", clip],
        check=True, capture_output=True, text=True, timeout=600)
    info = json.loads(probe.stdout)
    width = info["streams"][0]["width"]
    height = info["streams"][0]["height"]
    times = [frame.get("best_effort_timestamp_time") for frame in info.get("frames", [])]
    decode = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", clip, "-map", "0:v:0", "-fps_mode", "passthrough", "-f", "rawvideo",
         "-pix_fmt", "yuv420p", "-"],
        check=True, capture_output=True, timeout=600)
    raw = decode.stdout
    chroma_width = (width + 1) // 2
    chroma_height = (height + 1) // 2
    luma = width * height
    chroma = chroma_width * chroma_height
    frame_bytes = luma + 2 * chroma
    count = len(raw) // frame_bytes
    if count * frame_bytes != len(raw):
        sys.exit("nv12-hashes: %d bytes is not a whole number of %d-byte frames" % (len(raw), frame_bytes))
    for index in range(count):
        frame = raw[index * frame_bytes:(index + 1) * frame_bytes]
        cb = frame[luma:luma + chroma]
        cr = frame[luma + chroma:]
        interleaved = bytearray(2 * chroma)
        interleaved[0::2] = cb
        interleaved[1::2] = cr
        digest = hashlib.sha256()
        digest.update(frame[:luma])
        digest.update(interleaved)
        stamp = -1
        if index < len(times) and times[index] is not None:
            stamp = round(float(times[index]) * 1000000)
        print("%d %s" % (stamp, digest.hexdigest()))


main()
