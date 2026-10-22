<!-- awesome-plan project=zedbsd record=ws284-design -->

# WS284 YOLOX-Nano の推論の設計（C と Vulkan compute）

- 親: [ws284](ws.md)、Phase: [ws284-p001](phase001/phase.md)。第 1 版 2026-10-23 P1。
- 範囲: model の構造、重みの入手・変換・配布、library `libyolox`（Vulkan compute と CPU の参照の実装）、試験、Pi 5 の見積もり、Phase の分け方。code は書かない。

## 0. 出典と確かめた事

| 物 | 版・識別 | 使い方 |
| --- | --- | --- |
| YOLOX の source | github.com/Megvii-BaseDetection/YOLOX、main の先頭 `6ddff4824372906469a7fae2dc3206c7aa4bbaee`（2025-06-08） | 構造を読んだ file: `exps/default/yolox_nano.py`、`yolox/exp/yolox_base.py`、`yolox/models/{network_blocks,darknet,yolo_pafpn,yolo_head,yolox}.py`、`yolox/utils/{boxes,demo_utils,model_utils}.py`、`yolox/data/data_augment.py`、`tools/{demo,export_onnx}.py`、`demo/ONNXRuntime/onnx_inference.py`。scratchpad に取得し commit しない |
| 重み | release `0.1.1rc0` の `yolox_nano.pth`、7,694,953 byte、SHA-256 `cd28f55fbbc1829f99d9ac9b38a16d259a22889739c8728ea877610201feff7b`（2026-10-23 に取得して計算） | 変換の元。README の表の「YOLOX-Nano 416、mAP 25.8、0.91 M、1.08 G」の link 先 |
| ONNX | 同じ release の `yolox_nano.onnx`、3,659,407 byte、SHA-256 `c789161ed43c8269fcd4e67c67eeeb4e80c622da2eb296a20bc6007bd18a0b7d` | 参照の結果の照合の副（§4 T1） |
| LICENSE | 同じ commit の `LICENSE`、11,371 byte、SHA-256 `0ec3668d3274bcf29e8a29e9576d5a2cd96fc78d3c5bec4387355a796e5d9088`。Apache-2.0、「Copyright (c) 2021-2022 Megvii Inc.」。repository に NOTICE は無い（2026-10-23 に 404 を確認） | 配る LICENSE |

確かめた事（2026-10-23、host）:

- checkpoint は `torch.save` の zip 形式（974 entry、`archive/data.pkl` と `archive/data/N`）。key は `start_epoch`・`model`・`optimizer`・`amp`。torch を使わず、許す class を `collections.OrderedDict`・`torch._utils._rebuild_tensor_v2`・`torch.*Storage` だけにした unpickler で `model` を読んだ（他の class は拒否）。conv・BN の tensor の名前と形は §1 の構造と全て一致し、全て FloatStorage（fp32）。conv の重みと head の bias の合計 896,767、BN を畳んだ後の parameter は 904,463（README の 0.91 M と合う）。
- 表（付録 A）と数は [phase001/graph.py](phase001/graph.py)（source を写した graph）で出した。416 で 0.5226 G MAC（= 1.045 GFLOP。README の 1.08 G は thop が BN と活性化も数えた値）。

未確認: PyTorch で model を実際に動かした出力（host に torch が無い。p002 で venv を作って取る）。Pi 5 の QPU の数と clock（[WS279 design](../ws279/design.md) §8 U8）。

## 1. network

### 1.1 設定

`yolox_nano.py`: depth 0.33、width 0.25、`depthwise=True`、test_size 416×416、act SiLU、class 80、BN の **eps = 1e-3**（`get_model` の `init_yolo` が全ての BatchNorm2d に設定する。checkpoint には eps が入っていないので変換で 1e-3 を使う。PyTorch の既定 1e-5 で畳むと出力がずれる）。

- channel: base = int(64 × 0.25) = 16。dark2〜5 は 32・64・128・256。PAFPN の in_channels × width は 64・128・256。head の幅 int(256 × 0.25) = 64。
- 深さ: base_depth = max(round(0.33 × 3), 1) = 1。CSPLayer の bottleneck の数は dark2 1、dark3 3、dark4 3、dark5 1、PAFPN の 4 つは round(3 × 0.33) = 1。

### 1.2 部品（`network_blocks.py`）

| 部品 | 中身 |
| --- | --- |
| BaseConv(cin, cout, k, s) | Conv2d(k、stride s、pad (k−1)/2、bias 無し) → BN → SiLU。畳んだ後は conv（bias 付き）＋SiLU の 1 op |
| DWConv(cin, cout, k, s) | BaseConv(cin, cin, k, s, groups=cin)（depthwise）→ BaseConv(cin, cout, 1, 1)（pointwise）。Nano では `Conv = DWConv` の場所（dark2〜5 の先頭の stride 2、bottleneck の conv2、PAFPN の bu_conv、head の cls・reg の conv）がこれ |
| Bottleneck(c, c, shortcut) | conv1 = BaseConv 1×1（c→c、expansion 1.0）→ conv2 = DWConv 3×3 → shortcut かつ cin = cout なら入力を足す |
| CSPLayer(cin, cout, n, shortcut) | hid = cout/2。x1 = conv1 1×1(cin→hid) → n 個の Bottleneck、x2 = conv2 1×1(cin→hid)、**cat(x1, x2)**（この順）→ conv3 1×1(2hid→cout)。backbone は shortcut 有り（dark5 だけ無し）、PAFPN の 4 つは無し |
| SPPBottleneck(c, c) | conv1 1×1(c→c/2) → MaxPool 5・9・13（stride 1、pad k/2）→ **cat(x, p5, p9, p13)** → conv2 1×1(2c→c) |
| Focus（stem） | 入力 x[..., ::2, ::2]（左上）、x[..., 1::2, ::2]（左下）、x[..., ::2, 1::2]（右上）、x[..., 1::2, 1::2]（右下）を **左上・左下・右上・右下** の順に channel 方向に並べ（3→12、縦横 1/2）、BaseConv **3×3**（12→16、stride 1、groups 1、depthwise ではない） |

### 1.3 構造（416×416、C×H×W）

| 段 | 中身 | 出力 | MMAC |
| --- | --- | --- | --- |
| 入力 | BGR、0〜255 の fp32（§1.7） | 3×416×416 | |
| stem | Focus → conv3×3 12→16 + SiLU | 16×208×208 | 74.8 |
| dark2 | DWConv s2 16→32、CSP(32, n=1) | 32×104×104 | 36.4 |
| dark3 | DWConv s2 32→64、CSP(64, n=3) → **C3** | 64×52×52 | 47.4 |
| dark4 | DWConv s2 64→128、CSP(128, n=3) → **C4** | 128×26×26 | 45.9 |
| dark5 | DWConv s2 128→256、SPP(256)、CSP(256, n=1, shortcut 無し) → **C5** | 256×13×13 | 61.3 |
| lateral_conv0 | 1×1 256→128（= **F0**） | 128×13×13 | 5.5 |
| C3_p4 | up2×(F0) と C4 を cat（up が先）→ CSP(256→128) | 128×26×26 | 39.2 |
| reduce_conv1 | 1×1 128→64（= **F1**） | 64×26×26 | 5.5 |
| C3_p3 | up2×(F1) と C3 を cat → CSP(128→64) = **P3** | 64×52×52 | 39.6 |
| bu_conv2 + C3_n3 | DWConv s2 64→64、cat(それ, F1) → CSP(128→128) = **P4** | 128×26×26 | 31.2 |
| bu_conv1 + C3_n4 | DWConv s2 128→128、cat(それ, F0) → CSP(256→256) = **P5** | 256×13×13 | 30.9 |
| head ×3 | 各 level: stem 1×1 (C→64)、cls: DWConv×2 → 1×1 64→80、reg: DWConv×2 → 1×1 64→4 と 1×1 64→1（obj、reg と同じ入力） | 85×H×W | 105.0 |

level は P3（52×52、stride 8）・P4（26×26、16）・P5（13×13、32）。全 op の行は付録 A。

### 1.4 op の数（416）

| 種類 | 数 | MMAC | 備考 |
| --- | --- | --- | --- |
| conv 3×3 dense | 1 | 74.8 | stem だけ（全体の 14%） |
| depthwise 3×3 | 30 | 18.7 | stride 2 が 6（dark2〜5、bu_conv ×2） |
| conv 1×1 | 82 | 429.1 | うち活性化無しの head の予測 9 |
| concat | 13 | | CSPLayer の中 8（backbone 4・PAFPN 4）、SPP 1、PAFPN の段の間 4 |
| add（残差） | 7 | | dark2 1、dark3 3、dark4 3 |
| maxpool | 3 | | 5・9・13 |
| upsample nearest ×2 | 2 | | |
| space-to-depth | 1 | | Focus |
| 計 | 139 op、conv 113 | 522.6 | parameter（畳んだ後）904,463、fp32 で 3.45 MiB |

320×320 では 309.2 MMAC（0.59 倍）、parameter は同じ。


### 1.5 op の意味（実装で合わせる点）

- **BN の畳み込み**: scale = γ / sqrt(var + 1e-3)、w' = w × scale（出力 channel ごと）、b' = β − mean × scale。畳みは float64 で計算し fp32 に丸める（§2.2、結果を決定的にする）。
- **SiLU**: x × sigmoid(x) = x / (1 + exp(−x))。x が大きな負で exp が inf になっても −0 になり、NaN にならない（shader でも同じ式）。
- **upsample**: nearest、scale 2。out[y][x] = in[y/2][x/2]（整数の割り算）。
- **maxpool**: stride 1、pad k/2 の pad は −∞ 扱い（窓は画像の中に切り詰め）。5→9→13 は「5 を 1・2・3 回重ねる」と完全に同じ値になる（切り詰めた窓の合成が [i−4, i+4] ∩ 画像 になる）ので、GPU は 5×5 を 3 回並べる（SPPF の形）。
- **concat**: channel 方向。順は §1.2・§1.3 の通り（CSP は bottleneck の側が先、PAFPN は upsample・bu_conv の側が先、SPP は元・5・9・13）。
- **Focus**: 12 channel の番号 = patch × 3 + 色（patch は左上 0・左下 1・右上 2・右下 3、色は入力の順）。

### 1.6 head の decode と NMS（`yolo_head.py` の `decode_outputs`、`boxes.py` の `postprocess`）

- 各 level の出力を [reg 4, obj 1, cls 80] の順に channel で並べ（85）、level 0・1・2 の順に、各 level は y の行ごと x の順に anchor を並べる: 2704 + 676 + 169 = **3549 anchor**（320 では 1600 + 400 + 100 = 2100）。
- grid は (x, y)。cx = (reg0 + x) × stride、cy = (reg1 + y) × stride、w = exp(reg2) × stride、h = exp(reg3) × stride。obj と cls は sigmoid。
- postprocess: box を (cx−w/2, cy−h/2, cx+w/2, cy+h/2) に、class は cls の最大（同じ値は小さい番号）、score = obj × cls の最大。score ≥ 閾値の物を残し NMS。
- NMS は torchvision の `nms`/`batched_nms` の意味: score の降順に取り、既に取った物との IoU > 閾値の物を捨てる。面積は (x2−x1)×(y2−y1)（demo_utils の numpy 版の「+1」は使わない）。`tools/demo.py` は class を区別しない（`class_agnostic=True`）、閾値 conf 0.3・nms 0.3。評価（`yolox_base.py`）は conf 0.01・nms 0.65・class ごと。
- library の既定は demo と同じ（conf 0.3、nms 0.3、class を区別しない）。全て引数で変えられる（§3.2）。box は入力の画像の座標（÷ r、§1.7）で返し、切り詰めない（参照と同じ。表示の側で切る）。

### 1.7 前処理（`data_augment.py` の `preproc`、`ValTransform(legacy=False)`）

- r = min(416 / 高さ, 416 / 幅)。画像を (幅 × r, 高さ × r)（int で切り捨て）に bilinear（cv2 の INTER_LINEAR）で縮め、416×416 の **左上** に置き、残りを **114** で埋める。
- channel は cv2 の読んだ **BGR** のまま、0〜255 の fp32。平均・分散で割らない（legacy=False）。
- zedBSD は RGB で受ける。変換で stem の重みの入力 channel を並べ替え（新しい channel patch×3+c に元の patch×3+(2−c) の重み）、実行時の入れ替えを無くす（§2.2）。bilinear は cv2 の固定小数点の係数と少し違うので、試験では参照の前処理の済んだ画像を入力にし（§4）、前処理は別に許容差で確かめる。

## 2. 重み

### 2.1 元と license

- `https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_nano.pth`（§0 の size と SHA-256）。COCO 2017 train で学習した物。
- license: repository の Apache-2.0。release の重みに別の license の記載は無い（README・release の本文に無い）ので、repository の Apache-2.0 で配られた物として扱う（解釈。§7 D4 でユーザーが決める）。

### 2.2 変換の script（host で 1 回、build では動かさない）

`plan/ws284/tools/yolox-convert.py`（p002 で作る。python3 の標準 library だけ、torch 不要）:

1. 入力の .pth の size と SHA-256 を §0 の値と照合し、違えば止まる。
2. zip を開き、`archive/data.pkl` を制限した unpickler（§0 の 3 つだけを許す）で読み、`model` の tensor を `archive/data/N` から fp32 little-endian で取り出す（stride が連続である事、要素数と storage の大きさを確かめる）。
3. source の graph（`graph.py` を発展させた表）の順に、BN を畳み（§1.5、eps 1e-3、float64）、stem の入力を RGB の順に並べ替える。head の reg（4）と obj（1）は別の op のまま（library が融合を決める）。
4. §2.3 の形式で書く。fp32 の file を作り、`--fp16` で重みを fp16（IEEE binary16、最近接偶数への丸め、`struct` の `e`）にした file も作れる。
5. 同じ入力からは同じ byte（決定的）。2 回動かし SHA-256 が同じ事を p002 の試験にする。

### 2.3 file の形式「ZNET」版 1（little-endian、offset は file の先頭から、各 section は 64 byte 境界）

header（256 byte）:

| offset | 型 | 中身 |
| --- | --- | --- |
| 0 | char[4] | `ZNET` |
| 4 | u32 | 版 = 1 |
| 8 | u32 | header の大きさ = 256 |
| 12 | u32 | flag: bit 0 = 重みが fp16（0 なら fp32） |
| 16 | u32 | tensor の数 |
| 20 | u32 | op の数 |
| 24 / 28 | u32 | tensor 表 / op 表の offset |
| 32 / 36 | u32 | data の offset / 大きさ |
| 40 / 44 | u32 | 名前の表の offset / 大きさ（NUL 終端の文字列の並び） |
| 48 | u32 | 入力の tensor の番号 |
| 52 | u32 | 入力の色の順: 0 = RGB |
| 56 | u32 | 既定の入力の大きさ = 416 |
| 60 | u32 | 入力の大きさの倍数 = 32 |
| 64 | u32 | pad の値 = 114 |
| 68 | u32 | class の数 = 80 |
| 72 | u32 | level の数 = 3 |
| 76 | u32[4] | level の stride = 8, 16, 32, 0 |
| 92 | u32[4][3] | level ごとの (reg, obj, cls) の tensor の番号 |
| 140 | u8[20] | 予約 = 0 |
| 160 | u8[32] | 元の .pth の SHA-256 |
| 192 | u8[32] | data の section の SHA-256 |
| 224 | char[32] | model の名前 `yolox-nano`（NUL で埋める） |

tensor 表（1 つ 16 byte）: channel の数、空間の約数（入力の大きさ ÷ この値が縦横、1〜32）、flag（bit 0 入力、bit 1 head の出力）、予約。

op 表（1 つ 64 byte、16 個の u32）: 種類（1 space-to-depth、2 conv、3 maxpool、4 upsample nearest ×2、5 add、6 concat）、活性化（0 無し、1 SiLU）、kernel、stride、groups、入力の数、入力の tensor ×4、出力の tensor、重みの offset（data の中の byte）、重みの要素数、bias の offset、bias の要素数、名前の offset。

重みは conv ごとに PyTorch の形 [cout][cin/groups][k][k]、bias は [cout]。file は GPU の配置に依らない形にする（shader の配置を変えても配る file を変えなくてよい。library が load の時に並べ替える、§3.4）。op は topological の順。

大きさ: fp32 で約 3.5 MiB、fp16 で約 1.8 MiB。

library の load の確かめ（file は外から来る data として扱う）: magic・版・header の大きさ、各表と data が file の中に収まる事、数の上限（op ≤ 1024、tensor ≤ 1024、channel ≤ 4096）、tensor の番号が範囲内で入力が先に作られている事、op ごとの形の整合（conv の cin = 入力の channel、groups が割り切る、重みの要素数 = cout×cin/groups×k×k、空間の約数が stride に合う、concat の channel の和、add の形の一致）、level の tensor の channel が 4・1・class の数。data の SHA-256 は load で計算しない（入手の時に package が size と SHA-256 を確かめる、§2.4。header の値は由来の記録と試験の照合に使う）。

### 2.4 配布

firmware と同じ形にする（[userland/firmware/zedbsd-firmware.mk](../../userland/firmware/zedbsd-firmware.mk)、各 package が size と SHA-256 を宣言して確かめる）。

- awemorris/zedBSD-firmware に directory `yolox-nano/` を作り、`yolox-nano.znet`（fp32）、`LICENSE`（§0 の Apache-2.0 の原文、byte 単位で同じ）、`NOTICE`（zedBSD が書く、§2.5）を置く。repository への push は Q1・ユーザーの操作（担当は file を作るだけ）。
- 木の中の package `userland/firmware/yolox-nano/`（名前 `yolox-nano-weights`、§7 D2 で場所を決める）: Makefile（firmware の package と同じ書き方、`$(ZEDBSD_FIRMWARE_REPOSITORY_URL)/yolox-nano/FILE` から取り、size と SHA-256 を確かめて `build/sources/` の cache に置く）、`.notice`、`.manifest`（元の URL・SHA-256、変換の script と commit、出来た file の SHA-256）。
- upstream への fallback は無い（変換後の byte は zedBSD の repository にしか無い）。revision が空なら package は取れないと error で言う。.pth を取って build の中で変換する道は作らない（依頼の指示、§7 D1）。
- install: `/usr/share/yolox/yolox-nano.znet`、`/usr/share/licenses/yolox-nano-weights/{LICENSE,NOTICE}`、`/usr/share/zedbsd/packages/yolox-nano-weights.manifest`。全ての arch で選べる（data だけ）。
- Apache-2.0 は使う前の同意を要しないので、license-agree.sh の yes/no は出さない（`LICENSE_PROMPT n` 相当、§7 D2）。

### 2.5 license の監査の覚え

- Apache-2.0 §4: (a) LICENSE の写しを付ける → `LICENSE` を install。(b) 変えた file に変えた事を目立つように書く → NOTICE と manifest に「BN を畳んだ、stem の入力 channel を RGB の順に並べ替えた、形式を ZNET に変えた（fp16 版は値を丸めた）、他の値は変えていない」と書き、header に元の SHA-256 を入れる。(c) 著作権表示を残す → NOTICE に「YOLOX, Copyright (c) 2021-2022 Megvii Inc., Apache-2.0」。(d) 元に NOTICE が無いので引き継ぐ物は無い。
- library・変換の script・shader は zedBSD の code（Zlib）。YOLOX の Python の code は写さない（構造は事実として読み、名前・書き方は自分で書く）。graph の層の名前（`dark3.1.m.0` など）は checkpoint の key と対応を取るための data として表に使う。
- COCO: 重みは COCO 2017 で学習。COCO の注釈は CC BY 4.0、画像は Flickr の各者の license。zedBSD は COCO の画像も注釈も配らない（重みだけ）。学習済みの重みに COCO の license が及ぶかは法的に決まっていない点があり、業界では repository の license で配るのが普通 → 記録して §7 D4 でユーザーが受け入れを決める。
- 試験の画像（§4）は COCO val2017 の中で license の番号 7（No known copyright restrictions）か 8（United States Government Work）の物だけを選び、image id・Flickr の URL・license を `tests/data/README` に書く。

## 3. library `libyolox`

### 3.1 場所と build

- `userland/desktop/libyolox/`（libmedia と同じ並び、§7 D3）、共有 library `/lib/libyolox.so`、公開 header `yolox.h`、`exports.map` で `yolox_*` だけを出す。arch は amd64（QEMU・host の照合）と rpi5（rpi4 は WS283 の 4.2 が通れば足す）。
- 依存は libc・libm・dlopen だけ。libvulkan は link せず libmedia の `vkvideo-runtime.c` と同じく `dlopen("libvulkan.so.1")`、無ければ `libvulkan.so` → `vkGetInstanceProcAddr` から関数を引く。Vulkan が無い・compute が無い時も CPU で動く。
- Linux の host の build（`Makefile.linux`、`KEILAND_LINUX_LIBRARY`）を持ち、host の試験（§4）は host の cc と host の Vulkan の header・loader（lavapipe）で行う。
- shader は木の慣習（terminal・files・monitor など）に合わせる: `shaders/*.comp` と `shaders/regenerate.py`（`glslc --target-env=vulkan1.0 -O` と `spirv-val`）で作った `shaders.h` を commit し、build は shader の compiler を要しない。host に glslc・spirv-val・glslangValidator がある（2026-10-23 確認）。toolchain は変えない。

### 3.2 API（`yolox.h`、案）

```c
struct yolox_model;   /* 読んだ ZNET。device に依らない、不変 */
struct yolox_runner;  /* 1 つの backend と入力の大きさの実行の器 */

enum yolox_backend { YOLOX_BACKEND_AUTO, YOLOX_BACKEND_VULKAN, YOLOX_BACKEND_CPU };
enum yolox_pixel_format { YOLOX_PIXEL_RGB8, YOLOX_PIXEL_RGBA8, YOLOX_PIXEL_BGRA8 };

struct yolox_image {            /* host の画像、または VkBuffer の中の画像の形 */
	enum yolox_pixel_format format;
	uint32_t width, height, row_bytes;
};

struct yolox_detection {
	float x1, y1, x2, y2;       /* 入力の画像の座標（切り詰めない） */
	float score;                /* obj × class */
	uint32_t class_index;       /* 0〜79、yolox_class_name() */
};

struct yolox_runner_config {
	enum yolox_backend backend;
	uint32_t input_size;        /* 32 の倍数、0 は model の既定（416） */
	float score_threshold;      /* 既定 0.3 */
	float nms_threshold;        /* 既定 0.3 */
	int class_aware;            /* 0 = class を区別しない（既定） */
	/* 任意: app の Vulkan の device を共有する（WS285 の debayer と同じ device）。
	   全て NULL なら library が自分で instance と device を作る */
	PFN_vkGetInstanceProcAddr get_instance_proc_addr;
	VkInstance instance; VkPhysicalDevice physical_device; VkDevice device;
	uint32_t queue_family; VkQueue queue;
};

int yolox_model_load(const char *path, struct yolox_model **model);
void yolox_model_free(struct yolox_model *model);
const char *yolox_class_name(uint32_t class_index);
const char *yolox_reason(int code);

int yolox_runner_create(const struct yolox_model *model, const struct yolox_runner_config *config, struct yolox_runner **runner);
enum yolox_backend yolox_runner_backend(const struct yolox_runner *runner);
void yolox_runner_free(struct yolox_runner *runner);

/* 同期: host の画像から検出 */
int yolox_detect(struct yolox_runner *runner, const struct yolox_image *image, const void *pixels,
	struct yolox_detection *detections, uint32_t capacity, uint32_t *count);

/* 非同期（Vulkan だけ）: device の VkBuffer の画像を入力に submit し、後で結果を取る。
   wait は入力を書いた仕事の semaphore（無ければ VK_NULL_HANDLE） */
int yolox_submit_buffer(struct yolox_runner *runner, const struct yolox_image *image,
	VkBuffer buffer, VkDeviceSize offset, VkSemaphore wait);
int yolox_collect(struct yolox_runner *runner, uint64_t timeout_ns,
	struct yolox_detection *detections, uint32_t capacity, uint32_t *count);

/* 試験・調べ: 最後の実行の tensor を host に写す（runner を debug で作った時だけ全 tensor が残る） */
int yolox_runner_read_tensor(struct yolox_runner *runner, uint32_t tensor, float *values, size_t count);
```

- 戻り値は 0 か負の番号（`yolox_reason` で文）。in-flight は 1 つ（collect の前の次の submit は error）。WS285 が 2 つ重ねたい時は runner を 2 つ作る（重みは model を共有、device memory の重みは runner ごと。重みの共有は後の最適化）。
- 入力は VkBuffer だけ（VkImage は v1 では受けない。debayer は buffer に書く、§7 D7）。

### 3.3 中の作り

- `model.c`: ZNET の読み込みと確かめ（§2.3）、重みは host の fp32 の配列にする（fp16 の file はここで fp32 に戻す）。
- `graph.c`: op の列から実行の計画を作る: tensor の形（入力の大きさから）、融合（下）、memory の割り当て（§3.5）。backend に依らない。
- 融合（GPU・CPU の両方で同じ計画を使う。CPU の参照は融合しない道も持つ）: (1) Bottleneck の add を pointwise の conv の後処理に入れる（残差の入力を足してから書く）。(2) concat は割り当てで消す（作る側が concat の buffer の channel の位置に直に書く）。(3) upsample は concat の buffer の位置に書く小さな op。(4) maxpool 9・13 は 5 の重ね（§1.5）。(5) Focus は stem の conv が入力の index を読み替えて消す。(6) head の reg と obj は 1 つの 1×1（出力 5、8 に詰める）。
- `cpu.c`: CPU の参照（§3.8）。`vulkan.c`・`vulkan-runtime.c`: Vulkan の backend（§3.6）。`decode.c`: decode の後半と NMS（CPU、両方の backend で共通）。`classes.c`: COCO 80 の名前。

### 3.4 tensor の配置

- GPU: 「C4」= channel を 4 つずつ束ね、[⌈C/4⌉][H][W][4] の fp32（1 pixel 1 束が vec4、16 byte）。全ての concat の部分の channel は 4 の倍数なので、concat の部分は連続した区間になり、channel の位置 c の部分は offset (c/4)×H×W×16 に書くだけでよい。入力の 3 channel は 4 に詰める（4 番目は 0）。head の 80・5 は 80・8。
- 重みは load の時に並べ替える: 1×1 は [co/4][ci/4] の mat4（co の 4 × ci の 4）、depthwise は [c/4][9] の vec4、stem は [co/4][ky][kx][ci/4] の mat4、bias は [co/4] の vec4。
- CPU の参照は NCHW の素直な配置（GPU と違う code・配置にして、照合の基準として独立させる）。

### 3.5 memory の計画

- 直列の鎖だけなら 2 つの buffer の ping-pong で済むが、この graph は CSP の枝（x2）、PAFPN の横の線（C3・C4・F0・F1）、SPP の 4 つが長く生きるので、ping-pong では足りない。1 つの大きな VkBuffer（arena）の中に、tensor の生きる区間（作る op から最後に読む op まで）で first-fit の offset を割り当てる。concat の部分は concat の buffer の一部（生きる区間は部分と concat の和）。
- 416 の fp32 で、生きている tensor の和の最大は concat の別名で 4.62 MiB（別名無しで 6.60 MiB、全 tensor の和 45.3 MiB）。first-fit の断片を見て arena は 6〜8 MiB の見込み。320 では 2.73 MiB。重み（C4 に詰めて約 3.6 MiB）、入力の staging、出力（3549 × 24 byte）を足して device memory は 416 で 15 MiB 未満。
- offset は `minStorageBufferOffsetAlignment` に揃える。各 binding の範囲は最大の tensor（416 で 2.64 MiB）以下で、`maxStorageBufferRange` の下限（128 MiB）より十分小さい。
- debug の runner（試験用）は tensor ごとに別の buffer で全部残し、`yolox_runner_read_tensor` で層ごとの照合ができる。

### 3.6 Vulkan の backend

shader（GLSL 450 の compute、SPIR-V 1.0、`shaders/`）:

| shader | 仕事 | dispatch（416） |
| --- | --- | --- |
| `preprocess.comp` | host・device の RGB8/RGBA8/BGRA8 の画像 → letterbox（bilinear、114 の pad、左上）→ C4 の入力 | 1、416×416 |
| `stem.comp` | Focus の読み替え＋3×3 dense conv 12→16＋SiLU | 1 |
| `conv1x1.comp` | C4 の 1×1。1 invocation が出力 4 channel（1 束）× 画素 P 個（specialization constant、既定 4）を作り、ci/4 の束ごとに mat4 × vec4 を足す。後処理: bias、SiLU の有無、残差の加算の有無、出力の channel の位置（concat） | 82 |
| `dwconv3x3.comp` | depthwise 3×3、stride 1・2、1 invocation が 1 画素の 1 束（vec4）、SiLU | 30 |
| `maxpool5.comp` | 5×5 stride 1（−∞ の pad）。3 回並べ、各出力を SPP の concat の位置に書く | 3 |
| `upsample2x.comp` | nearest ×2、concat の位置に書く | 2 |
| `decode.comp` | level ごと: anchor ごとに sigmoid（obj、cls 80）、最大の class、score、box の decode、÷ r。3549 個の記録 {x1, y1, x2, y2, score, class} を host から見える buffer に書く（閾値で切らない、atomic を使わない） | 3 |

- 計 122 dispatch。command buffer は runner を作る時に 1 回記録し（graph は固定）、毎回の submit は push constant を変えない（入力の大きさと r は uniform の小さな buffer に書く）。依存のある dispatch の間に compute→compute の memory barrier、最後に compute→host。
- 結果: host が 85 KB の記録を読み、CPU で閾値と NMS（`decode.c`）。GPU で NMS はしない（候補は数百以下で CPU が速い）。
- local size は 64（V3D の 16 の倍数、上限 256 より小さい）。shared memory は使わない（V3D の shared は memory 上にあり TMU 経由なので速くならない。§5）。必要なら後で specialization constant で tile を変える。
- device の共有（§3.2 の config）: app が渡した device・queue を使い、library は自分の pipeline・buffer・command pool だけを持つ。

### 3.7 Vulkan の実装（WS283 の compute）に求める物

zedBSD の libvulkan は Vulkan 1.0 の core だけ（[README](../../userland/desktop/libvulkan/README.md)）。shader はそれで足りる形に限る:

- storage buffer（std430）、push constant ≤ 128 byte、uniform buffer、specialization constant、`GlobalInvocationID`、local size 64（1 次元・2 次元）。
- GLSL.std.450 の `Exp`（sigmoid・SiLU・exp）。任意で `PackHalf2x16`/`UnpackHalf2x16`（fp16 の詰め、§3.9）。
- 使わない: shared memory、barrier()、subgroup、atomic、16 bit・8 bit の storage の拡張、image・sampler、timestamp（あれば計測に使う）。
- 1 回の submit に 122 個の dispatch と barrier。dispatch ごとの CSD の job と cache の clean（[WS279 design](../ws279/design.md) §2.5「CSD の後に L2T の clean」）が 1 つずつ数十 µs かかると 1 frame に数 ms になる → WS283 に「同じ submit の中の CSD の job の間は clean を省けるか」を調べてもらう（§8 R2）。

### 3.8 CPU の参照

- C の素直な loop（NCHW、fp32、op ごとの関数: conv（k・stride・groups の一般形）、maxpool の一般形（5・9・13 を直に、GPU の重ねと独立に）、upsample、add、concat の写し、space-to-depth）。前処理は bilinear の C 版、decode・NMS は `decode.c` を共有。
- 二つの役: (1) 照合の基準（PyTorch の参照の結果と比べ、GPU の各 op の試験の期待値を作る）、(2) compute が無い所での実行（Pi 5 の A76 で -O2 の自動 vector 化で 1 frame 100〜300 ms の見込み、遅いが動く）。NEON の手書き・thread は範囲の外（§7 D9）。

### 3.9 精度

- 既定は全て fp32（重み・活性化・計算）。
- 任意（p006）: 活性化と重みを `packHalf2x16` で fp16 に詰めて memory の量を半分にする（計算は fp32、Vulkan 1.0 の core で足りる）。memory の帯域が律速と分かった時だけ。
- 配る file は fp32 だけ（§7 D8）。fp16 の file は変換の script の選択肢として残す。

## 4. 試験

| 番号 | 何を | どこで | 基準 |
| --- | --- | --- | --- |
| T0 | 変換の script: 2 回の出力の SHA-256 が同じ、Python の読み手で読み戻して畳んだ値（float64 で計算した期待）と一致、stem の RGB の並べ替え | host（Python） | 一致 |
| T1 | 参照の結果を作る（1 回）: host の venv（`build/ws284/venv/`、torch の CPU 版・torchvision・opencv-python-headless・loguru・numpy、YOLOX は commit 6ddff482 を PYTHONPATH に。C の拡張は build しない）で `exp.get_model()`＋checkpoint＋eval、`ValTransform(legacy=False)` → 出力。副に onnxruntime で ONNX の出力も取り、両者が 1e-4 以内である事を見る | host | 作る |
| T2 | CPU の参照 vs PyTorch: T1 の前処理の済んだ入力から、head の生の出力（3549×85）と検出 | host | 生の出力の差の最大 ≤ 2e-3、検出の集合が同じ（class が同じ、IoU ≥ 0.99、score の差 ≤ 1e-3。score が閾値から 1e-3 以内、IoU が NMS の閾値から 1e-3 以内の境の物は除く） |
| T3 | GPU の op ごと: 乱数の tensor（seed 固定）で 7 つの shader の各形（stride・残差・concat の位置・活性化の有無）を CPU の参照の op と比べる | host の lavapipe（llvmpipe、2026-10-23 確認） | 差の最大 ≤ 1e-4（相対） |
| T4 | GPU の全体 vs CPU の参照: 同じ入力で生の出力と検出 | host の lavapipe | T2 と同じ基準 |
| T5 | 前処理: GPU の preprocess と CPU の C 版、JPEG から cv2 の preproc の結果との差 | host | GPU と CPU は 1/255 以内、cv2 とは平均の差 ≤ 1.0・検出の集合（T2 の基準を IoU ≥ 0.9 に緩める） |
| T6 | fp16 の詰め（使う時だけ） | host の lavapipe | 検出の集合（IoU ≥ 0.95、score の差 ≤ 0.02） |
| T7 | Pi 5 の実機: T3・T4 と速さ（op ごとと全体、10 回の中央値）。zedBSD の image に libyolox・重み・試験の program `yolox-test`（入力の file と参照を読み比べる）を入れる | Pi 5（WS283 の後、T1 の担当に依頼） | T3・T4 と同じ |
| T8 | Pi 4・amd64 の QEMU（Venus）: 任意 | T1 の担当 | 同じ |

試験の data（`plan/ws284/tests/data/`、§7 D5）: COCO val2017 の license 7・8 の画像 3 枚（人・車・動物などが写る物）。各画像について、T1 の前処理の済んだ 416×416 の RGB（binary PPM、約 520 KB）、参照の検出（文、demo の閾値と評価の閾値の 2 組）、生の出力の一部（7 個おきの anchor と score の上位 100 の 85 値、fp32、約 200 KB）。元の JPEG（T5 用）。合計は約 2.5 MB。全 tensor の dump は host の `build/ws284/ref/` に作り commit しない（層ごとの不一致を探す時に T1 の script で作り直す）。

QEMU・実機の試験は担当が自分で起動せず、T1 の担当に依頼する（AGENTS.md）。host の試験（T0〜T6）は担当が行う。

## 5. Pi 5 の見積もり

前提（未確認の値は WS279 §8 U8 で確かめる）: V3D 7.1、QPU 12、clock 800 MHz 前後。QPU は 1 clock に 4 lane（16 幅を 4 clock）で add と mul を 1 つずつ（FMA は無い）→ 1 MAC = mul + add で、最大 12 × 4 × 0.8 G = **38.4 G MAC/s**。memory は LPDDR4X、32 bit、約 17 GB/s。

| 項目 | 416 | 320 |
| --- | --- | --- |
| MAC | 0.523 G | 0.309 G |
| 最大の速さでの時間 | 13.6 ms | 8.1 ms |
| 効率 10〜25% の時 | 54〜136 ms | 32〜80 ms |
| dispatch の固定の分（122 × 20〜100 µs） | 2〜12 ms | 2〜12 ms |
| 活性化の読み書き（fp32、約 130 MB/frame、帯域の半分を得て） | 約 15 ms（上の効率に含まれる見込み） | 約 9 ms |
| 見込み（1 frame） | **60〜150 ms（7〜16 fps）** | **35〜90 ms（11〜28 fps）** |

- 効率の幅の理由: V3D の memory の読みは TMU（latency が大きく、thread の切り替えで隠す）、1×1 の conv は 1 invocation が出力 4 × 画素 4 の 16 個の足し算の器で register に収まる（2 thread の mode）が、ci の 1 束ごとに重み mat4（vec4 4 回）と入力 4 画素（vec4 4 回）の 8 回の読みで 64 MAC（MAC 8 つに vec4 の読み 1 回、多くは L2T に当たる見込み）。これは測るまで分からない。
- 比べ: A76 ×4（2.4 GHz、NEON の FMA 4 幅 × 2）は最大 76.8 G MAC/s で、よく作った CPU の実装（ncnn 等）なら V3D より速い見込み（§8 R1）。ユーザーの目標は Vulkan compute なので V3D を主にし、CPU の参照は fallback。
- fallback の順: (1) 入力 320（同じ重み、0.59 倍、精度は少し落ちる。公式の 320 の mAP は無いので WS285 で見て決める）、さらに 256（0.38 倍）。(2) 検出を N frame に 1 回にし、表示は camera の速さで前の box を出す（WS285）。(3) fp16 の詰め（§3.9）。(4) depthwise と pointwise の融合（DWConv の 2 dispatch を 1 つ、中間の書き戻しを無くす）。(5) CPU の backend。
- WS285 の目標の案: 416 で 5 fps 以上、320 で 10 fps 以上（p006 の測定で見直す）。

## 6. Phase

| Phase | 内容 | 依存 | 見込み |
| --- | --- | --- | --- |
| p001 | 設計（この文書） | — | — |
| p002 | 重み: 変換の script（§2.2）、ZNET の仕様の確定、T0、T1 の venv と参照の生成、試験の画像の選定と license の記録、firmware の repository の `yolox-nano/` の file（LICENSE・NOTICE・.znet）の用意、木の中の package `yolox-nano-weights` の Makefile（revision は Q1 が repository に push した後に入れる） | p001、D1・D2・D4・D5 | 4〜6 LW |
| p003 | libyolox の骨格と CPU の参照: `yolox.h`・model の読み込みと確かめ・graph の計画（形・融合・割り当て）・CPU の op・decode・NMS・class 名、Linux の host の build、T2 | p002 | 6〜8 LW |
| p004 | Vulkan の backend: dlopen・device の作成と共有・arena・重みの並べ替え・7 つの shader と regenerate.py・command buffer・submit/collect、T3〜T5（lavapipe） | p003 | 8〜10 LW |
| p005 | zedBSD の package: libyolox（amd64・rpi5）、重みの package の image への導入、試験の program `yolox-test`、試験の image の config.mk（`plan/ws284/tests/`）、T8（amd64 QEMU、任意）を T1 の担当に依頼 | p004 | 3〜4 LW |
| p006 | Pi 5: T7、op ごとの時間、tile（P）・local size の調整、必要なら fp16 の詰め・dw＋pw の融合、416/320 の推し | p005、WS283 の Pi 5 の compute | 6〜10 LW |
| p007 | 規約の全文の見直し（coding-style.md）と license の監査（§2.5、shader・script を含む） | p006 | 2〜3 LW |

計 29〜41 LW。p002〜p004 は host だけで進められ、WS283 を待たない。WS285 の設計（p001）は §3.2 の API と §5 の見込みを使える。

## 7. ユーザーの判断

| 番号 | 問い | 推し |
| --- | --- | --- |
| D1 | 重みの置き場所 | awemorris/zedBSD-firmware の `yolox-nano/` に変換後の fp32 の file と LICENSE・NOTICE。build は firmware と同じく size・SHA-256 を確かめて取る（upstream の fallback は無い）。代案: build で upstream の .pth を取り標準 library の Python で変換（依頼の指示に反するので推さない） |
| D2 | package の名前・menu の場所・同意の問い | `yolox-nano-weights`、木の中は `userland/firmware/yolox-nano/`（firmware の仕組みを使う。menu は Firmware の下か新しい「Models」）、Apache-2.0 なので yes/no の同意は出さない |
| D3 | library の名前・場所 | `libyolox`、`userland/desktop/libyolox/` |
| D4 | 重みの license の解釈（release に別の記載が無く repository の Apache-2.0 とみなす）と COCO で学習した重みを配る事の受け入れ | 受け入れる（§2.5 の記録を残す） |
| D5 | 試験の data の置き場所（約 2.5 MB） | `plan/ws284/tests/data/`。代案: firmware の repository の `yolox-nano/tests/` |
| D6 | 既定の閾値 | demo と同じ conf 0.3・NMS 0.3・class を区別しない |
| D7 | GPU の入力 | VkBuffer だけ（VkImage は後） |
| D8 | 精度 | fp32 の file だけを配る。fp16 は p006 で帯域が律速の時だけ活性化に使う |
| D9 | CPU の backend の速さ | 素直な C（参照と fallback）だけ。NEON・thread の最適化は範囲の外（V3D が遅すぎた時に別 WS） |

## 8. 危うさと未確認

| 番号 | 中身 | 対処 |
| --- | --- | --- |
| R1 | V3D が CPU（A76 ×4）より遅い見込みがある | p006 で測り、§5 の fallback。結果をユーザーに示す |
| R2 | dispatch ごとの CSD の job と L2T の clean の固定の時間 | WS283 の設計に伝える（§3.7）。多ければ op の融合で dispatch を減らす |
| R3 | V3D の SFU の exp の精度が lavapipe と違い、閾値の境の検出が変わる | T7 で生の出力の差を見る。基準は境を除く（§4 T2） |
| R4 | PyTorch の参照の venv（torch の CPU 版、数百 MB）を host に入れる | `build/ws284/venv/` に閉じ、build には使わない。入らなければ onnxruntime と公式の ONNX で代える（ONNX の出力が PyTorch と同じ事は T1 の副で見る） |
| R5 | 重みの license の解釈（D4） | ユーザーの判断 |
| U1 | Pi 5 の QPU の数・clock | WS279 U8 |
| U2 | zedBSD の V3D の compute の compiler が `Exp`・vec4 の storage の読み書き・specialization constant を通すか | WS283 の設計と T7 |

## 付録 A. 全 op の表（416、[phase001/graph.py](phase001/graph.py) の出力）

名前は checkpoint の key（`backbone.backbone.` と `backbone.` を除いた物）。params は BN を畳んだ後（重み＋bias）。

| 名前 | op | 入力 | 出力 | params | MMAC |
| --- | --- | --- | --- | --- | --- |
| focus | space2depth | 3x416x416 | 12x208x208 | 0 | 0.00 |
| stem | conv3x3+silu | 12x208x208 | 16x208x208 | 1744 | 74.76 |
| dark2.0.dconv | dw3x3/s2+silu | 16x208x208 | 16x104x104 | 160 | 1.56 |
| dark2.0.pconv | conv1x1+silu | 16x104x104 | 32x104x104 | 544 | 5.54 |
| dark2.1.conv1 | conv1x1+silu | 32x104x104 | 16x104x104 | 528 | 5.54 |
| dark2.1.conv2 | conv1x1+silu | 32x104x104 | 16x104x104 | 528 | 5.54 |
| dark2.1.m.0.conv1 | conv1x1+silu | 16x104x104 | 16x104x104 | 272 | 2.77 |
| dark2.1.m.0.conv2.dconv | dw3x3+silu | 16x104x104 | 16x104x104 | 160 | 1.56 |
| dark2.1.m.0.conv2.pconv | conv1x1+silu | 16x104x104 | 16x104x104 | 272 | 2.77 |
| dark2.1.m.0.add | add | 16x104x104,16x104x104 | 16x104x104 | 0 | 0.00 |
| dark2.1.cat | concat | 16x104x104,16x104x104 | 32x104x104 | 0 | 0.00 |
| dark2.1.conv3 | conv1x1+silu | 32x104x104 | 32x104x104 | 1056 | 11.08 |
| dark3.0.dconv | dw3x3/s2+silu | 32x104x104 | 32x52x52 | 320 | 0.78 |
| dark3.0.pconv | conv1x1+silu | 32x52x52 | 64x52x52 | 2112 | 5.54 |
| dark3.1.conv1 | conv1x1+silu | 64x52x52 | 32x52x52 | 2080 | 5.54 |
| dark3.1.conv2 | conv1x1+silu | 64x52x52 | 32x52x52 | 2080 | 5.54 |
| dark3.1.m.0.conv1 | conv1x1+silu | 32x52x52 | 32x52x52 | 1056 | 2.77 |
| dark3.1.m.0.conv2.dconv | dw3x3+silu | 32x52x52 | 32x52x52 | 320 | 0.78 |
| dark3.1.m.0.conv2.pconv | conv1x1+silu | 32x52x52 | 32x52x52 | 1056 | 2.77 |
| dark3.1.m.0.add | add | 32x52x52,32x52x52 | 32x52x52 | 0 | 0.00 |
| dark3.1.m.1.conv1 | conv1x1+silu | 32x52x52 | 32x52x52 | 1056 | 2.77 |
| dark3.1.m.1.conv2.dconv | dw3x3+silu | 32x52x52 | 32x52x52 | 320 | 0.78 |
| dark3.1.m.1.conv2.pconv | conv1x1+silu | 32x52x52 | 32x52x52 | 1056 | 2.77 |
| dark3.1.m.1.add | add | 32x52x52,32x52x52 | 32x52x52 | 0 | 0.00 |
| dark3.1.m.2.conv1 | conv1x1+silu | 32x52x52 | 32x52x52 | 1056 | 2.77 |
| dark3.1.m.2.conv2.dconv | dw3x3+silu | 32x52x52 | 32x52x52 | 320 | 0.78 |
| dark3.1.m.2.conv2.pconv | conv1x1+silu | 32x52x52 | 32x52x52 | 1056 | 2.77 |
| dark3.1.m.2.add | add | 32x52x52,32x52x52 | 32x52x52 | 0 | 0.00 |
| dark3.1.cat | concat | 32x52x52,32x52x52 | 64x52x52 | 0 | 0.00 |
| dark3.1.conv3 | conv1x1+silu | 64x52x52 | 64x52x52 | 4160 | 11.08 |
| dark4.0.dconv | dw3x3/s2+silu | 64x52x52 | 64x26x26 | 640 | 0.39 |
| dark4.0.pconv | conv1x1+silu | 64x26x26 | 128x26x26 | 8320 | 5.54 |
| dark4.1.conv1 | conv1x1+silu | 128x26x26 | 64x26x26 | 8256 | 5.54 |
| dark4.1.conv2 | conv1x1+silu | 128x26x26 | 64x26x26 | 8256 | 5.54 |
| dark4.1.m.0.conv1 | conv1x1+silu | 64x26x26 | 64x26x26 | 4160 | 2.77 |
| dark4.1.m.0.conv2.dconv | dw3x3+silu | 64x26x26 | 64x26x26 | 640 | 0.39 |
| dark4.1.m.0.conv2.pconv | conv1x1+silu | 64x26x26 | 64x26x26 | 4160 | 2.77 |
| dark4.1.m.0.add | add | 64x26x26,64x26x26 | 64x26x26 | 0 | 0.00 |
| dark4.1.m.1.conv1 | conv1x1+silu | 64x26x26 | 64x26x26 | 4160 | 2.77 |
| dark4.1.m.1.conv2.dconv | dw3x3+silu | 64x26x26 | 64x26x26 | 640 | 0.39 |
| dark4.1.m.1.conv2.pconv | conv1x1+silu | 64x26x26 | 64x26x26 | 4160 | 2.77 |
| dark4.1.m.1.add | add | 64x26x26,64x26x26 | 64x26x26 | 0 | 0.00 |
| dark4.1.m.2.conv1 | conv1x1+silu | 64x26x26 | 64x26x26 | 4160 | 2.77 |
| dark4.1.m.2.conv2.dconv | dw3x3+silu | 64x26x26 | 64x26x26 | 640 | 0.39 |
| dark4.1.m.2.conv2.pconv | conv1x1+silu | 64x26x26 | 64x26x26 | 4160 | 2.77 |
| dark4.1.m.2.add | add | 64x26x26,64x26x26 | 64x26x26 | 0 | 0.00 |
| dark4.1.cat | concat | 64x26x26,64x26x26 | 128x26x26 | 0 | 0.00 |
| dark4.1.conv3 | conv1x1+silu | 128x26x26 | 128x26x26 | 16512 | 11.08 |
| dark5.0.dconv | dw3x3/s2+silu | 128x26x26 | 128x13x13 | 1280 | 0.19 |
| dark5.0.pconv | conv1x1+silu | 128x13x13 | 256x13x13 | 33024 | 5.54 |
| dark5.1.conv1 | conv1x1+silu | 256x13x13 | 128x13x13 | 32896 | 5.54 |
| dark5.1.m.0 | maxpool5 | 128x13x13 | 128x13x13 | 0 | 0.00 |
| dark5.1.m.1 | maxpool9 | 128x13x13 | 128x13x13 | 0 | 0.00 |
| dark5.1.m.2 | maxpool13 | 128x13x13 | 128x13x13 | 0 | 0.00 |
| dark5.1.cat | concat | 128x13x13,128x13x13,128x13x13,128x13x13 | 512x13x13 | 0 | 0.00 |
| dark5.1.conv2 | conv1x1+silu | 512x13x13 | 256x13x13 | 131328 | 22.15 |
| dark5.2.conv1 | conv1x1+silu | 256x13x13 | 128x13x13 | 32896 | 5.54 |
| dark5.2.conv2 | conv1x1+silu | 256x13x13 | 128x13x13 | 32896 | 5.54 |
| dark5.2.m.0.conv1 | conv1x1+silu | 128x13x13 | 128x13x13 | 16512 | 2.77 |
| dark5.2.m.0.conv2.dconv | dw3x3+silu | 128x13x13 | 128x13x13 | 1280 | 0.19 |
| dark5.2.m.0.conv2.pconv | conv1x1+silu | 128x13x13 | 128x13x13 | 16512 | 2.77 |
| dark5.2.cat | concat | 128x13x13,128x13x13 | 256x13x13 | 0 | 0.00 |
| dark5.2.conv3 | conv1x1+silu | 256x13x13 | 256x13x13 | 65792 | 11.08 |
| lateral_conv0 | conv1x1+silu | 256x13x13 | 128x13x13 | 32896 | 5.54 |
| up0 | upsample2x | 128x13x13 | 128x26x26 | 0 | 0.00 |
| cat_p4 | concat | 128x26x26,128x26x26 | 256x26x26 | 0 | 0.00 |
| C3_p4.conv1 | conv1x1+silu | 256x26x26 | 64x26x26 | 16448 | 11.08 |
| C3_p4.conv2 | conv1x1+silu | 256x26x26 | 64x26x26 | 16448 | 11.08 |
| C3_p4.m.0.conv1 | conv1x1+silu | 64x26x26 | 64x26x26 | 4160 | 2.77 |
| C3_p4.m.0.conv2.dconv | dw3x3+silu | 64x26x26 | 64x26x26 | 640 | 0.39 |
| C3_p4.m.0.conv2.pconv | conv1x1+silu | 64x26x26 | 64x26x26 | 4160 | 2.77 |
| C3_p4.cat | concat | 64x26x26,64x26x26 | 128x26x26 | 0 | 0.00 |
| C3_p4.conv3 | conv1x1+silu | 128x26x26 | 128x26x26 | 16512 | 11.08 |
| reduce_conv1 | conv1x1+silu | 128x26x26 | 64x26x26 | 8256 | 5.54 |
| up1 | upsample2x | 64x26x26 | 64x52x52 | 0 | 0.00 |
| cat_p3 | concat | 64x52x52,64x52x52 | 128x52x52 | 0 | 0.00 |
| C3_p3.conv1 | conv1x1+silu | 128x52x52 | 32x52x52 | 4128 | 11.08 |
| C3_p3.conv2 | conv1x1+silu | 128x52x52 | 32x52x52 | 4128 | 11.08 |
| C3_p3.m.0.conv1 | conv1x1+silu | 32x52x52 | 32x52x52 | 1056 | 2.77 |
| C3_p3.m.0.conv2.dconv | dw3x3+silu | 32x52x52 | 32x52x52 | 320 | 0.78 |
| C3_p3.m.0.conv2.pconv | conv1x1+silu | 32x52x52 | 32x52x52 | 1056 | 2.77 |
| C3_p3.cat | concat | 32x52x52,32x52x52 | 64x52x52 | 0 | 0.00 |
| C3_p3.conv3 | conv1x1+silu | 64x52x52 | 64x52x52 | 4160 | 11.08 |
| bu_conv2.dconv | dw3x3/s2+silu | 64x52x52 | 64x26x26 | 640 | 0.39 |
| bu_conv2.pconv | conv1x1+silu | 64x26x26 | 64x26x26 | 4160 | 2.77 |
| cat_n3 | concat | 64x26x26,64x26x26 | 128x26x26 | 0 | 0.00 |
| C3_n3.conv1 | conv1x1+silu | 128x26x26 | 64x26x26 | 8256 | 5.54 |
| C3_n3.conv2 | conv1x1+silu | 128x26x26 | 64x26x26 | 8256 | 5.54 |
| C3_n3.m.0.conv1 | conv1x1+silu | 64x26x26 | 64x26x26 | 4160 | 2.77 |
| C3_n3.m.0.conv2.dconv | dw3x3+silu | 64x26x26 | 64x26x26 | 640 | 0.39 |
| C3_n3.m.0.conv2.pconv | conv1x1+silu | 64x26x26 | 64x26x26 | 4160 | 2.77 |
| C3_n3.cat | concat | 64x26x26,64x26x26 | 128x26x26 | 0 | 0.00 |
| C3_n3.conv3 | conv1x1+silu | 128x26x26 | 128x26x26 | 16512 | 11.08 |
| bu_conv1.dconv | dw3x3/s2+silu | 128x26x26 | 128x13x13 | 1280 | 0.19 |
| bu_conv1.pconv | conv1x1+silu | 128x13x13 | 128x13x13 | 16512 | 2.77 |
| cat_n4 | concat | 128x13x13,128x13x13 | 256x13x13 | 0 | 0.00 |
| C3_n4.conv1 | conv1x1+silu | 256x13x13 | 128x13x13 | 32896 | 5.54 |
| C3_n4.conv2 | conv1x1+silu | 256x13x13 | 128x13x13 | 32896 | 5.54 |
| C3_n4.m.0.conv1 | conv1x1+silu | 128x13x13 | 128x13x13 | 16512 | 2.77 |
| C3_n4.m.0.conv2.dconv | dw3x3+silu | 128x13x13 | 128x13x13 | 1280 | 0.19 |
| C3_n4.m.0.conv2.pconv | conv1x1+silu | 128x13x13 | 128x13x13 | 16512 | 2.77 |
| C3_n4.cat | concat | 128x13x13,128x13x13 | 256x13x13 | 0 | 0.00 |
| C3_n4.conv3 | conv1x1+silu | 256x13x13 | 256x13x13 | 65792 | 11.08 |
| head.stems.0 | conv1x1+silu | 64x52x52 | 64x52x52 | 4160 | 11.08 |
| head.cls_convs.0.0.dconv | dw3x3+silu | 64x52x52 | 64x52x52 | 640 | 1.56 |
| head.cls_convs.0.0.pconv | conv1x1+silu | 64x52x52 | 64x52x52 | 4160 | 11.08 |
| head.cls_convs.0.1.dconv | dw3x3+silu | 64x52x52 | 64x52x52 | 640 | 1.56 |
| head.cls_convs.0.1.pconv | conv1x1+silu | 64x52x52 | 64x52x52 | 4160 | 11.08 |
| head.cls_preds.0 | conv1x1 | 64x52x52 | 80x52x52 | 5200 | 13.84 |
| head.reg_convs.0.0.dconv | dw3x3+silu | 64x52x52 | 64x52x52 | 640 | 1.56 |
| head.reg_convs.0.0.pconv | conv1x1+silu | 64x52x52 | 64x52x52 | 4160 | 11.08 |
| head.reg_convs.0.1.dconv | dw3x3+silu | 64x52x52 | 64x52x52 | 640 | 1.56 |
| head.reg_convs.0.1.pconv | conv1x1+silu | 64x52x52 | 64x52x52 | 4160 | 11.08 |
| head.reg_preds.0 | conv1x1 | 64x52x52 | 4x52x52 | 260 | 0.69 |
| head.obj_preds.0 | conv1x1 | 64x52x52 | 1x52x52 | 65 | 0.17 |
| head.stems.1 | conv1x1+silu | 128x26x26 | 64x26x26 | 8256 | 5.54 |
| head.cls_convs.1.0.dconv | dw3x3+silu | 64x26x26 | 64x26x26 | 640 | 0.39 |
| head.cls_convs.1.0.pconv | conv1x1+silu | 64x26x26 | 64x26x26 | 4160 | 2.77 |
| head.cls_convs.1.1.dconv | dw3x3+silu | 64x26x26 | 64x26x26 | 640 | 0.39 |
| head.cls_convs.1.1.pconv | conv1x1+silu | 64x26x26 | 64x26x26 | 4160 | 2.77 |
| head.cls_preds.1 | conv1x1 | 64x26x26 | 80x26x26 | 5200 | 3.46 |
| head.reg_convs.1.0.dconv | dw3x3+silu | 64x26x26 | 64x26x26 | 640 | 0.39 |
| head.reg_convs.1.0.pconv | conv1x1+silu | 64x26x26 | 64x26x26 | 4160 | 2.77 |
| head.reg_convs.1.1.dconv | dw3x3+silu | 64x26x26 | 64x26x26 | 640 | 0.39 |
| head.reg_convs.1.1.pconv | conv1x1+silu | 64x26x26 | 64x26x26 | 4160 | 2.77 |
| head.reg_preds.1 | conv1x1 | 64x26x26 | 4x26x26 | 260 | 0.17 |
| head.obj_preds.1 | conv1x1 | 64x26x26 | 1x26x26 | 65 | 0.04 |
| head.stems.2 | conv1x1+silu | 256x13x13 | 64x13x13 | 16448 | 2.77 |
| head.cls_convs.2.0.dconv | dw3x3+silu | 64x13x13 | 64x13x13 | 640 | 0.10 |
| head.cls_convs.2.0.pconv | conv1x1+silu | 64x13x13 | 64x13x13 | 4160 | 0.69 |
| head.cls_convs.2.1.dconv | dw3x3+silu | 64x13x13 | 64x13x13 | 640 | 0.10 |
| head.cls_convs.2.1.pconv | conv1x1+silu | 64x13x13 | 64x13x13 | 4160 | 0.69 |
| head.cls_preds.2 | conv1x1 | 64x13x13 | 80x13x13 | 5200 | 0.87 |
| head.reg_convs.2.0.dconv | dw3x3+silu | 64x13x13 | 64x13x13 | 640 | 0.10 |
| head.reg_convs.2.0.pconv | conv1x1+silu | 64x13x13 | 64x13x13 | 4160 | 0.69 |
| head.reg_convs.2.1.dconv | dw3x3+silu | 64x13x13 | 64x13x13 | 640 | 0.10 |
| head.reg_convs.2.1.pconv | conv1x1+silu | 64x13x13 | 64x13x13 | 4160 | 0.69 |
| head.reg_preds.2 | conv1x1 | 64x13x13 | 4x13x13 | 260 | 0.04 |
| head.obj_preds.2 | conv1x1 | 64x13x13 | 1x13x13 | 65 | 0.01 |
