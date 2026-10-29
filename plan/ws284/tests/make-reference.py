# zedBSD
# Copyright (C) 2026 Awe Morris
#
# SPDX-License-Identifier: Zlib
"""Writes the PyTorch reference of YOLOX-Nano for some images (design section 4, T1).

For each image and input size: <name>-<size>.ppm (the preprocessed RGB input,
ValTransform(legacy=False), 0..255), <name>-<size>.head.f32 (the head output
before decode, anchors x 85, little-endian float32), the largest difference to
the ONNX model's output, and <name>-<size>.det.txt (detections at conf 0.3 /
nms 0.3, the demo's, and at conf 0.001 / nms 0.65, the evaluation's).
"""

import argparse
import os
import struct
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--yolox", required=True)
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--onnx", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("images", nargs="+")
    arguments = parser.parse_args()
    sys.path.insert(0, arguments.yolox)

    import cv2
    import numpy
    import onnxruntime
    import torch
    from yolox.data.data_augment import ValTransform
    from yolox.exp import get_exp
    from yolox.utils import postprocess

    exp = get_exp(os.path.join(arguments.yolox, "exps/default/yolox_nano.py"), None)
    model = exp.get_model()
    model.load_state_dict(torch.load(arguments.checkpoint, map_location="cpu", weights_only=False)["model"])
    model.eval()
    session = onnxruntime.InferenceSession(arguments.onnx, providers=["CPUExecutionProvider"])
    summary = open(os.path.join(arguments.output, "summary.txt"), "w")

    for path in arguments.images:
        name = os.path.splitext(os.path.basename(path))[0]
        image = cv2.imread(path)
        for size in (416, 320):
            transform = ValTransform(legacy=False)
            tensor, _ = transform(image, None, (size, size))
            ratio = min(size / image.shape[0], size / image.shape[1])

            # The preprocessed input as RGB.
            rgb = tensor[::-1].transpose(1, 2, 0).clip(0, 255).astype(numpy.uint8)
            with open(os.path.join(arguments.output, "%s-%d.ppm" % (name, size)), "wb") as ppm:
                ppm.write(b"P6\n%d %d\n255\n" % (size, size))
                ppm.write(rgb.tobytes())

            # The head output before decode.
            batch = torch.from_numpy(tensor).unsqueeze(0).float()
            with torch.no_grad():
                model.head.decode_in_inference = False
                raw = model(batch)[0].numpy()
                model.head.decode_in_inference = True
                decoded = model(batch)
            with open(os.path.join(arguments.output, "%s-%d.head.f32" % (name, size)), "wb") as head:
                head.write(struct.pack("<%df" % raw.size, *raw.reshape(-1).tolist()))

            # The ONNX model at 416 only (its input size is fixed).
            if size == 416:
                onnx = session.run(None, {session.get_inputs()[0].name: tensor[None, :, :, :]})[0][0]
                summary.write("%s %d onnx max |d| %.3g\n" % (name, size, float(numpy.abs(onnx - raw).max())))

            # The detections at the two thresholds, in the input image's coordinates.
            with open(os.path.join(arguments.output, "%s-%d.det.txt" % (name, size)), "w") as text:
                for conf, nms in ((0.3, 0.3), (0.001, 0.65)):
                    result = postprocess(decoded.clone(), 80, conf, nms, class_agnostic=True)[0]
                    text.write("# conf %g nms %g\n" % (conf, nms))
                    if result is None:
                        continue
                    for row in result.tolist():
                        x1, y1, x2, y2 = (value / ratio for value in row[0:4])
                        text.write("%d %.6f %.4f %.4f %.4f %.4f\n" % (int(row[6]), row[4] * row[5], x1, y1, x2, y2))
            summary.write("%s %d anchors %d\n" % (name, size, raw.shape[0]))
    summary.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
