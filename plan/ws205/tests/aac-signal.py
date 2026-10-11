#!/usr/bin/env python3
# ws205-p003: writes the AAC encoder's test signals (16-bit little-endian, interleaved) and compares decoded PCM.
#   aac-signal.py make NAME RATE CHANNELS SECONDS OUT.raw   NAME: chord (tones and a sweep below 12 kHz), music
#       (noise under 12 kHz with clicks and a gated tone), square, silence
#   aac-signal.py snr REF.raw REF_FORMAT TEST.raw TEST_FORMAT CHANNELS DELAY LEAST   FORMAT: s16 or f32; DELAY: the
#       samples TEST is late; prints the SNR and fails under LEAST
#   aac-signal.py silent TEST.raw   fails unless every sample is 0
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import sys
import numpy as np


def make(name, rate, channels, seconds, out):
    n = int(rate * seconds)
    t = np.arange(n) / rate
    rng = np.random.default_rng(7)
    columns = []
    for channel in range(channels):
        if name == "chord":
            x = 0.25 * np.sin(2 * np.pi * (440 + 110 * channel) * t) + 0.15 * np.sin(2 * np.pi * (1250 - 300 * channel) * t)
            x += 0.1 * np.sin(2 * np.pi * (200 + 5000 * t / seconds) * t)
        elif name == "music":
            spectrum = np.fft.rfft(rng.normal(0, 0.1, n))
            spectrum[int(12000 * n / rate):] = 0
            x = np.fft.irfft(spectrum, n)
            x += 0.2 * np.sin(2 * np.pi * (220 + 55 * channel) * t) * (np.sin(2 * np.pi * 0.5 * t) > 0)
            for k in range(0, n, rate // 4):
                x[k:k + 200] += 0.5 * np.exp(-np.arange(min(200, n - k)) / 30) * rng.choice([-1, 1], min(200, n - k))
        elif name == "square":
            x = 0.9 * np.sign(np.sin(2 * np.pi * 440 * t))
        else:
            x = np.zeros(n)
        columns.append(np.clip(x, -1, 1))
    pcm = (np.stack(columns, axis=1) * 32767).astype("<i2")
    pcm.tofile(out)


def load(path, form, channels):
    if form == "f32":
        data = np.fromfile(path, dtype="<f4").astype(float) * 32768.0
    else:
        data = np.fromfile(path, dtype="<i2").astype(float)
    return data.reshape(-1, channels)


def snr(ref, ref_form, test, test_form, channels, delay, least):
    x = load(ref, ref_form, channels)
    y = load(test, test_form, channels)
    n = min(len(x), len(y) - delay)
    start = n // 8
    end = n - n // 8
    a = x[start:end]
    b = y[start + delay:end + delay]
    if np.sum(a * a) == 0.0:
        if np.sum(b * b) != 0.0:
            print("FAIL the reference is silent, the test is not")
            sys.exit(1)
        print("snr=silent both")
        return
    value = 10 * np.log10(np.sum(a * a) / max(np.sum((b - a) ** 2), 1e-30))
    print("snr=%.1f" % value)
    if value < least:
        print("FAIL snr under %.1f" % least)
        sys.exit(1)


if sys.argv[1] == "make":
    make(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), float(sys.argv[5]), sys.argv[6])
elif sys.argv[1] == "snr":
    snr(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], int(sys.argv[6]), int(sys.argv[7]), float(sys.argv[8]))
elif sys.argv[1] == "silent":
    data = np.fromfile(sys.argv[2], dtype="<i2")
    if np.any(data != 0):
        print("FAIL not silent: peak %d" % np.max(np.abs(data)))
        sys.exit(1)
    print("silent")
