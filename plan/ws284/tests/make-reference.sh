#!/bin/sh
# Makes the PyTorch reference of YOLOX-Nano (design section 4, T1) on the host:
# a venv in build/ws284/venv, YOLOX at commit 6ddff482 in build/ws284/yolox,
# the checkpoint and the ONNX model in build/ws284/ref, then for each image
# given and each input size (416, 320) the preprocessed input, the 3549x85
# (or 2100x85) head output before decode, the ONNX output for comparison, and
# the decoded detections at the demo and the evaluation thresholds.
#
#   sh plan/ws284/tests/make-reference.sh IMAGE.jpg...
#
# Needs the network.  Nothing is removed; the outputs go to a new directory
# under build/ws284/ref/.  The venv is not used by any build or image.
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
base="$repo/build/ws284"
venv="$base/venv"
yolox="$base/yolox"
mkdir -p "$base/ref"
out=$(mktemp -d "$base/ref/run.XXXXXX")

yolox_commit=6ddff4824372906469a7fae2dc3206c7aa4bbaee
pth_url=https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_nano.pth
pth_sha=cd28f55fbbc1829f99d9ac9b38a16d259a22889739c8728ea877610201feff7b
onnx_url=https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_nano.onnx
onnx_sha=c789161ed43c8269fcd4e67c67eeeb4e80c622da2eb296a20bc6007bd18a0b7d

# The venv, once, with the packages the reference imports.
if [ ! -x "$venv/bin/python3" ]; then
	python3 -m venv "$venv"
	"$venv/bin/pip" install --index-url https://download.pytorch.org/whl/cpu torch torchvision
	"$venv/bin/pip" install opencv-python-headless numpy loguru psutil tabulate packaging onnxruntime
fi
"$venv/bin/pip" freeze > "$out/pip-freeze.txt"

# YOLOX at the commit the design read, without its C extension.
if [ ! -d "$yolox/.git" ]; then
	git clone https://github.com/Megvii-BaseDetection/YOLOX.git "$yolox"
fi
git -C "$yolox" checkout -q "$yolox_commit"

# The checkpoint and the ONNX model, checked.
fetch() {
	url=$1
	sha=$2
	file="$base/ref/$(basename "$url")"
	if [ ! -f "$file" ]; then
		curl -fL --max-time 600 -o "$file.part" "$url"
		mv "$file.part" "$file"
	fi
	echo "$sha  $file" | sha256sum -c -
}
fetch "$pth_url" "$pth_sha"
fetch "$onnx_url" "$onnx_sha"

# The reference for every image and size.
PYTHONPATH="$yolox" timeout 3600 "$venv/bin/python3" -I "$repo/plan/ws284/tests/make-reference.py" \
    --yolox "$yolox" --checkpoint "$base/ref/yolox_nano.pth" --onnx "$base/ref/yolox_nano.onnx" \
    --output "$out" "$@"
echo "reference: $out"
