<!-- awesome-plan project=zedbsd record=ws284-design -->

# WS284 YOLOX-Nano の推論の設計（C と Vulkan compute）

- 親: [ws284](ws.md)、Phase: [ws284-p001](phase001/phase.md)。第 1 版 2026-10-23 P1。**第 2 版 2026-10-23 P1**（[review](phase001/review-20261023.md) の F1〜F22 とユーザーの判断を入れた。指摘ごとの対応は [phase.md](phase001/phase.md) の表）。
- 範囲: model の構造、重みの入手・変換・配布、library `libyolox`（Vulkan compute と CPU の参照の実装）、試験、Pi 5 の見積もり、Phase の分け方。code は書かない。
- ユーザーの判断（2026-10-23、原文）:「decisionは推しでいいです。重みはダウンロードしてビルドする形にしましょう。それならソースツリーにライセンスを混入させないはずです。」→ D1〜D9 は推しで決定。ただし重み（D1）は「build が公式の `yolox_nano.pth` を取得し、size と SHA-256 を確かめ、build の中で ZNET に変換する。変換した重みはどの repository にも置かない」に変わった（§2）。

## 0. 出典と確かめた事

| 物 | 版・識別 | 使い方 |
| --- | --- | --- |
| YOLOX の source | github.com/Megvii-BaseDetection/YOLOX、main の先頭 `6ddff4824372906469a7fae2dc3206c7aa4bbaee`（2025-06-08） | 構造を読んだ file: `exps/default/yolox_nano.py`、`yolox/exp/yolox_base.py`、`yolox/models/{network_blocks,darknet,yolo_pafpn,yolo_head,yolox}.py`、`yolox/utils/{boxes,demo_utils,model_utils}.py`、`yolox/data/data_augment.py`、`tools/{demo,export_onnx}.py`、`demo/ONNXRuntime/onnx_inference.py`。scratchpad に取得し commit しない |
| 重み | release `0.1.1rc0` の `yolox_nano.pth`、URL `https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_nano.pth`、**7,694,953 byte、SHA-256 `cd28f55fbbc1829f99d9ac9b38a16d259a22889739c8728ea877610201feff7b`**（2026-10-23 に取得して計算、review も一致を確認） | build が取得して変換する元（§2） |
| ONNX | 同じ release の `yolox_nano.onnx`、3,659,407 byte、SHA-256 `c789161ed43c8269fcd4e67c67eeeb4e80c622da2eb296a20bc6007bd18a0b7d`。decode の前の出力（`export_onnx.py` の `decode_in_inference` の既定 False、Exp の op が無い） | 試験だけ（T0 の initializer の照合、T1 の副）。build は使わない |
| LICENSE | 同じ commit の `LICENSE`、URL `https://raw.githubusercontent.com/Megvii-BaseDetection/YOLOX/6ddff4824372906469a7fae2dc3206c7aa4bbaee/LICENSE`、**11,371 byte、SHA-256 `0ec3668d3274bcf29e8a29e9576d5a2cd96fc78d3c5bec4387355a796e5d9088`**。Apache-2.0、「Copyright (c) 2021-2022 Megvii Inc.」。repository に NOTICE は無い（2026-10-23 に 404 を確認） | build が重みと一緒に取得して install する（tree には入れない） |

確かめた事（2026-10-23、host）:

- checkpoint は `torch.save` の zip 形式（974 entry、`archive/data.pkl` と `archive/data/N`）。key は `start_epoch`・`model`・`optimizer`・`amp`。torch を使わず、許す class を `collections.OrderedDict`・`torch._utils._rebuild_tensor_v2`・`torch.*Storage` だけにした unpickler で `model` を読んだ。conv・BN の tensor の名前と形は §1 の構造と全て一致し、全て FloatStorage（fp32）。BN を畳んだ後の parameter は 904,463（README の 0.91 M と合う）。
- BN の eps 1e-3 で畳んだ値は ONNX の initializer と最大の相対差 1.22e-7 で一致（1e-5 では 2.05）。ONNX の op は Conv 113・Add 7・MaxPool 3・Resize 2・Sigmoid 110（review の確かめ）。
- 表（付録 A）と数は [phase001/graph.py](phase001/graph.py) で出した。416 で 0.5226 G MAC。第 2 版の GPU の計画の数（§3.4〜§3.6、§5）は `python3 -I graph.py 416 gpu 8`（と `320 gpu 8`、`416 gpu 8 list`）の出力。
- Pi 5 の V3D は WS279 p002 の実機の記録で **V3D 7.1.10・12 QPU・1.15 GHz**（plan/ws279/phase002/phase.md（fb1f6afee の `plan/ws279/phase002/phase.md`））。§5 はこの値を使う（第 1 版の 800 MHz は誤り）。

未確認: PyTorch で model を実際に動かした出力（host に torch が無い。p002 の T1）。V3D の TMU の実効の帯域と L2T の大きさ、dispatch の 1 回の費用（WS283 p007 の R5）。

## 1. network

### 1.1 設定

`yolox_nano.py`: depth 0.33、width 0.25、`depthwise=True`、test_size 416×416、act SiLU、class 80、BN の **eps = 1e-3**（`get_model` の `init_yolo` が全ての BatchNorm2d に設定する。checkpoint には eps が入っていないので変換で 1e-3 を使う）。

- channel: base = int(64 × 0.25) = 16。dark2〜5 は 32・64・128・256。PAFPN の in_channels × width は 64・128・256。head の幅 int(256 × 0.25) = 64。
- 深さ: base_depth = max(round(0.33 × 3), 1) = 1。CSPLayer の bottleneck の数は dark2 1、dark3 3、dark4 3、dark5 1、PAFPN の 4 つは round(3 × 0.33) = 1。

### 1.2 部品（`network_blocks.py`）

| 部品 | 中身 |
| --- | --- |
| BaseConv(cin, cout, k, s) | Conv2d(k、stride s、pad (k−1)/2、bias 無し) → BN → SiLU。畳んだ後は conv（bias 付き）＋SiLU の 1 op |
| DWConv(cin, cout, k, s) | BaseConv(cin, cin, k, s, groups=cin)（depthwise）→ BaseConv(cin, cout, 1, 1)（pointwise）。Nano では dark2〜5 の先頭の stride 2、bottleneck の conv2、PAFPN の bu_conv、head の cls・reg の conv がこれ |
| Bottleneck(c, c, shortcut) | conv1 = BaseConv 1×1（c→c）→ conv2 = DWConv 3×3 → shortcut かつ cin = cout なら入力を足す（SiLU の**後**に足す、§1.5） |
| CSPLayer(cin, cout, n, shortcut) | hid = cout/2。x1 = conv1 1×1(cin→hid) → n 個の Bottleneck、x2 = conv2 1×1(cin→hid)、**cat(x1, x2)**（この順）→ conv3 1×1(2hid→cout)。backbone は shortcut 有り（dark5 だけ無し）、PAFPN の 4 つは無し |
| SPPBottleneck(c, c) | conv1 1×1(c→c/2) → MaxPool 5・9・13（stride 1、pad k/2）→ **cat(x, p5, p9, p13)** → conv2 1×1(2c→c) |
| Focus（stem） | x[..., ::2, ::2]（左上）、x[..., 1::2, ::2]（左下）、x[..., ::2, 1::2]（右上）、x[..., 1::2, 1::2]（右下）を **左上・左下・右上・右下** の順に channel 方向に並べ（3→12、縦横 1/2）、BaseConv **3×3**（12→16、stride 1、groups 1） |

### 1.3 構造（416×416、C×H×W）

| 段 | 中身 | 出力 | MMAC |
| --- | --- | --- | --- |
| 入力 | BGR（変換で RGB に、§1.7）、0〜255 の整数値の fp32 | 3×416×416 | |
| stem | Focus → conv3×3 12→16 + SiLU（GPU は 6×6 stride 2 の 1 つの conv、§3.3） | 16×208×208 | 74.8 |
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
| depthwise 3×3 | 30 | 18.7 | stride 2 が 6 |
| conv 1×1 | 82 | 429.1 | うち活性化無しの head の予測 9 |
| concat | 13 | | |
| add（残差） | 7 | | dark2 1、dark3 3、dark4 3 |
| maxpool | 3 | | 5・9・13 |
| upsample nearest ×2 | 2 | | |
| space-to-depth | 1 | | Focus |
| 計 | 139 op、conv 113 | 522.6 | parameter（畳んだ後）904,463、fp32 で 3.45 MiB |

320×320 では 309.2 MMAC（0.59 倍）、parameter は同じ。

### 1.5 op の意味（実装で合わせる点）

- **BN の畳み込み**: scale = γ / sqrt(var + 1e-3)、w' = w × scale（出力 channel ごと）、b' = β − mean × scale。float64 で計算し fp32 に丸める（最近接偶数、§2.2）。
- **SiLU**: x × sigmoid(x) = x / (1 + exp(−x))。x が大きな負で exp が inf になっても −0 になり NaN にならない（shader でも同じ式）。
- **残差**（F15）: Bottleneck の出力は `out = SiLU(acc + b) + residual`（pconv の SiLU の後に入力を足す。`Bottleneck.forward` の `y = conv2(conv1(x)); y = y + x`）。
- **upsample**: nearest、scale 2。out[y][x] = in[y >> 1][x >> 1]。
- **maxpool**: stride 1、窓は画像の中に切り詰め（pad は −∞ 扱い）。切り詰めた窓の最大は「座標を画像の中に clamp して読んだ窓の最大」と同じ（clamp で重複する値は窓の中の値）なので、GPU は −∞ を使わず座標を clamp する。5→9→13 は「5 を 1・2・3 回重ねる」と完全に同じ値になるので、GPU は 5×5 を 3 回並べる。
- **concat**: channel 方向。順は §1.2・§1.3 の通り（CSP は bottleneck の側が先、PAFPN は upsample・bu_conv の側が先、SPP は元・5・9・13）。
- **Focus**: 12 channel の番号 = patch × 3 + 色（patch は左上 0・左下 1・右上 2・右下 3）。

### 1.6 head の decode と NMS（`yolo_head.py` の `decode_outputs`、`boxes.py` の `postprocess`）

- 各 level の出力を [reg 4, obj 1, cls 80] の順に channel で並べ（85）、level 0・1・2 の順に、各 level は y の行ごと x の順に anchor を並べる: 2704 + 676 + 169 = **3549 anchor**（320 では 2100）。推論の時の head は obj と cls に sigmoid を掛けてから decode する（`decode_in_inference=False` の出力はこの 85 値、§4 T1）。
- grid は (x, y)。cx = (reg0 + x) × stride、cy = (reg1 + y) × stride、w = exp(reg2) × stride、h = exp(reg3) × stride。
- postprocess: box を (cx−w/2, cy−h/2, cx+w/2, cy+h/2) に、class は cls の最大（同じ値は小さい番号）、score = obj × cls の最大。score ≥ 閾値の物を残し NMS。
- **zedBSD の NMS の規則**（F19、torchvision の版の違いに依らないよう自分で決める）: 候補を score の降順、同じ score は anchor の番号の小さい順、さらに class の小さい順に並べる（安定で決定的）。先頭から取り、既に取った物との IoU > 閾値の物を捨てる。class を区別する時は class ごとに厳密に同じ手順（座標をずらす方法は使わない）。IoU は fp32 で inter / (area_a + area_b − inter)、面積は (x2−x1)×(y2−y1)（「+1」は使わない）、union が 0 なら IoU 0。
- library の既定は demo と同じ（conf 0.3、nms 0.3、class を区別しない、D6）。box は入力の画像の座標（network の座標を fp32 の r で割る。torch の `bboxes /= ratio` と同じく fp32）で返し、切り詰めない。
- GPU の decode は level ごとに cls の logit の最大を取ってから sigmoid を 1 回だけ計算する（sigmoid は単調なので最大の位置は同じ。ただし sigmoid の後の fp32 の値が同じになる 2 つの logit では、参照は小さい番号、zedBSD は大きい logit を選ぶ。試験は score の同点を除く、§4）。

### 1.7 前処理（F5、`data_augment.py` の `preproc`、`ValTransform(legacy=False)`）

参照（Python）の処理: `r = min(S / h, S / w)`（float64）、`cv2.resize(img, (int(w * r), int(h * r)), INTER_LINEAR).astype(np.uint8)`、S×S の 114 の画像の **左上** に置く、`float32` にする。network の入力は 0〜255 の**整数**の値。平均・分散で割らない。

zedBSD の定義（CPU の C 版と GPU で同じ。cv2 とは ±1 の許容差で合わせる）:

1. 大きさ: `double r = fmin((double)S / h, (double)S / w); dw = (uint32_t)(w * r); dh = (uint32_t)(h * r);`（IEEE binary64、Python と同じ順。amd64・aarch64 の C は FLT_EVAL_METHOD 0）。h ≤ 5000 で `int(h × (416 / h))` が 415 になる h が 260 個ある（23・45・46・90・92・93・165…、review が計算）。その時は 1 画素の 114 の帯が残る（参照と同じ）。dw か dh が 0 の画像、幅・高さが 16384 を越える画像は error。
2. 写像（横、縦も同じ）: scale = 1.0 / ((double)dw / w)（cv2 と同じ形）。各 dx について fx = (dx + 0.5) × scale − 0.5、sx = floor(fx)、a = fx − sx。sx < 0 なら sx = 0・a = 0、sx ≥ w − 1 なら sx = w − 1・a = 0。重み c1 = (int)lround(a × 2048)、c0 = 2048 − c1（cv2 の 11 bit の固定小数点に倣う）。第 2 の tap は min(sx + 1, w − 1)。
3. 値: v = ((s00·c0 + s01·c1)·d0 + (s10·c0 + s11·c1)·d1 + 2^21) >> 22（整数、0〜255 に収まる）。dx ≥ dw か dy ≥ dh の画素は 114。
4. 係数の表（dw 個の (sx, c1) と dh 個の (sy, d1)）は **host が double で計算**し、GPU には小さな buffer で渡す。GPU は整数の演算だけをするので、GPU と CPU の C 版は bit で一致する（T5）。cv2 の SIMD の経路は丸めが少し違う所がある（推測、OpenCV の source は今回読んでいない）ので、cv2 とは 0〜255 の尺度で ±1 を許す。
5. 色: zedBSD は RGB で受ける。変換で stem の重みの入力 channel を並べ替える（新しい channel patch×3+c に元の patch×3+(2−c) の重み）。114 の pad は全ての色で同じなので影響しない。

## 2. 重み（ユーザーの判断 D1: build で取得して変換）

### 2.1 元と license

- 元: §0 の `yolox_nano.pth`（COCO 2017 train で学習）。license は repository の Apache-2.0（release に別の記載は無い。D4 で受け入れ済み）。
- **tree にも、どの repository にも、変換した重みを置かない。** tree に入るのは zedBSD が書いた物（変換の script、package の Makefile、NOTICE の文、manifest）だけ。重みと LICENSE の文は build の時に upstream（または byte の同じ mirror）から取る。

### 2.2 変換の script（build の中で動く）

`userland/firmware/yolox-nano/yolox-convert.py`（p002 で作る、zedBSD の code、Zlib。F20: WS の完了で消えない場所）。python3 の標準 library（`zipfile`・`pickle`・`struct`・`hashlib`・`math`）だけ、torch 不要。

1. 入力の .pth の size と SHA-256 を §0 の値と照合し、違えば止まる（解析はその後。既知の byte だけを読む）。
2. zip を開き、`archive/data.pkl` を制限した unpickler（§0 の 3 つの class だけを許し、他は拒否）で読み、`model` の tensor を `archive/data/N` から fp32 little-endian で取り出す（stride が連続、要素数と storage の大きさを確かめる）。
3. 表（graph.py を発展させた op の列）の順に、BN を畳み（§1.5、eps 1e-3、float64）、stem の入力 channel を RGB の順に並べ替える。head の reg（4）と obj（1）は別の op のまま（融合は library の仕事）。
4. §2.3 の形式で fp32 の file を書く（D8: 配るのは fp32 だけ。`--fp16` は試験用の選択肢）。
5. 決定的: float64 の四則と `math.sqrt` は IEEE で正しく丸められ、`struct.pack('<f')` は最近接偶数で丸めるので、同じ入力からは Python の版・host に依らず同じ byte になる（推測ではなく IEEE の規定による。違う byte が出たら次の 6 で build が止まる）。
6. **build は出来た ZNET の size と SHA-256 を、package の Makefile に固定した値と照合する**（値は p002 で script を書いた後に決めて Makefile と manifest に書く）。違えば build は失敗し、file を使わない。
7. 呼び方: `$(PYTHON) -I userland/firmware/yolox-nano/yolox-convert.py --input <cache の .pth> --output <一時 file>`。入力は引数で渡し、取得した file の directory から何も import しない。

### 2.3 file の形式「ZNET」版 1（little-endian、offset は file の先頭から、各 section は 64 byte 境界）

header（256 byte）:

| offset | 型 | 中身 |
| --- | --- | --- |
| 0 | char[4] | `ZNET` |
| 4 | u32 | 版 = 1 |
| 8 | u32 | header の大きさ = 256 |
| 12 | u32 | flag: bit 0 = 重みと bias が fp16（0 なら fp32）。他の bit は 0 |
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
| 92 | u32[4][3] | level ごとの (reg, obj, cls) の tensor の番号（4 番目の組は 0） |
| 140 | u8[20] | 予約 = 0 |
| 160 | u8[32] | 元の .pth の SHA-256 |
| 192 | u8[32] | data の section の SHA-256 |
| 224 | char[32] | model の名前 `yolox-nano`（NUL で埋める） |

tensor 表（1 つ 16 byte）: channel の数、空間の約数（入力の大きさ ÷ この値が縦横、1〜32 の 2 の冪）、flag（bit 0 入力、bit 1 head の出力、他は 0）、予約 = 0。

op 表（1 つ 64 byte、16 個の u32）: 種類（1 space-to-depth、2 conv、3 maxpool、4 upsample nearest ×2、5 add、6 concat）、活性化（0 無し、1 SiLU）、kernel、stride、groups、入力の数、入力の tensor ×4、出力の tensor、重みの offset（data の中の byte）、重みの要素数、bias の offset、bias の要素数、名前の offset。

重みは conv ごとに PyTorch の形 [cout][cin/groups][k][k]、bias は [cout]。file は GPU の配置に依らない（library が load・runner の作成の時に並べ替える、§3.4）。op は topological の順。大きさは fp32 で約 3.5 MiB。

**load の確かめ**（F11、file は外から来る data として扱う。違反は全て `YOLOX_ERROR_FORMAT`）:

- file: 大きさ ≤ 64 MiB、magic・版・header の大きさ、flag と予約の field が 0（定義の外の bit）、各表・data・名前の表が file の中に収まる（offset＋大きさは 64 bit で計算）、section が 64 byte 境界。
- 数: op ≤ 1024、tensor ≤ 1024、channel ≤ 4096、class の数 = 80（名前の表は 80 だけ）、level の数 = 3、stride は 8・16・32、入力の大きさの倍数 = 32、既定の入力の大きさは 64〜1024 の 32 の倍数。
- op ごと: 種類と活性化が定義の値。conv の kernel ∈ {1, 3}、stride ∈ {1, 2}、groups ∈ {1, cin}（groups = cin の時は cout = cin）、cin = 入力の channel。maxpool の kernel ∈ {5, 9, 13}・stride 1。入力の数が種類に合う（conv・maxpool・upsample・space-to-depth は 1、add は 2、concat は 2〜4）。重みの要素数 = cout × (cin / groups) × k × k、bias の要素数 = cout（積は 64 bit）。重み・bias の offset が data の中にあり、要素の大きさ（fp32 は 4、fp16 は 2）の境界。名前の offset が名前の表の中で、NUL が表の中にある。
- graph: 各 tensor を作る op がちょうど 1 つ（入力の tensor を作る op は 0）、全ての op の入力は前の op（または入力の tensor）が作った物（これで循環も無い）。空間の約数が op に合う（conv stride 2 と space-to-depth は 2 倍、upsample は 1/2、他は同じ）。concat の channel の和、add の形の一致。level の (reg, obj, cls) の tensor が head の出力の flag を持ち、channel が 4・1・80、空間の約数が stride。
- runner を作る時（入力の大きさ S が決まった時）: S は 64〜1024 の 32 の倍数。最大の tensor の byte・arena の大きさが device の `maxStorageBufferRange` 以下、dispatch の group の数が `maxComputeWorkGroupCount` 以下（§3.5）。
- data の SHA-256 は load で計算しない（build が ZNET の SHA-256 を確かめる、§2.2 の 6。header の値は由来の記録と試験の照合に使う）。
- 負の試験（p003、T2b）: 壊れた magic・版、範囲の外の offset、u32 の溢れを起こす大きさ、未定義の flag、二重の生成、未生成の tensor の読み（循環）、形の不整合、範囲の外の kernel・stride・groups。

### 2.4 package と配布（F10）

package `yolox-nano-weights`、tree の中は `userland/firmware/yolox-nano/`（firmware の取得の仕組み `zedbsd-firmware.mk` を使う。menu は既存の Firmware の下、全ての arch で選べる data の package）。

- 取得: 2 つの file を、firmware の package と同じく `$(ZEDBSD_FIRMWARE_REPOSITORY_URL)/yolox-nano/FILE` から先に、失敗したら upstream（§0 の URL）から取り、size と SHA-256 を確かめる。
  - `yolox_nano.pth`（7,694,953 byte、`cd28f55f…ff7b`）
  - `LICENSE`（11,371 byte、`0ec3668d…9088`、upstream は §0 の raw の URL）
- これは firmware の repository の約束（「各 file を upstream と byte で同じに保ち、fetch は upstream に戻れる。repository は byte の出所だけを変え、受け入れる byte を変えない」、`zedbsd-firmware.mk` 1〜19 行）にそのまま合う。zedBSD-firmware の repository が後で `yolox-nano/` に同じ byte を置いてもよい（Q1・ユーザーの操作。担当は push しない）。置くまでは mirror の取得が失敗して upstream から取る（既存の package と同じ表示）。**変換した ZNET は mirror にも置かない。**
- cache: `build/sources/firmware/yolox-nano/0.1.1rc0/` に確かめた 2 つの file だけ（他の firmware の package と同じく、一時 directory に取って確かめてから rename、既存の cache は毎回確かめ直し、壊れていれば error）。
- 変換: cache の .pth から §2.2 で ZNET を build の出力の directory（`build/` の中、arch に依らない data。正確な path は p002）に作り、size と SHA-256 を確かめてから install の元にする。cache には派生物を置かない（cache は upstream の byte だけ）。
- install:
  - `/usr/share/yolox/yolox-nano.znet`
  - `/usr/share/licenses/yolox-nano-weights/LICENSE`（取得した Apache-2.0 の原文、byte で同じ）
  - `/usr/share/licenses/yolox-nano-weights/NOTICE`（tree の `userland/firmware/yolox-nano/yolox-nano-weights.notice`、zedBSD が書いた文、§2.5）
  - `/usr/share/zedbsd/packages/yolox-nano-weights.manifest`（tree の manifest: 元の URL・size・SHA-256、LICENSE の URL・SHA-256、変換の script の path、ZNET の size・SHA-256）
- `make download` は 2 つの file を取る（`ZEDBSD_USERLAND_DOWNLOAD_TARGETS` に足す）。
- **同意の問い（yes/no）**: 推しは「出さない」（§7 D10、ユーザーに確かめる）。理由: firmware の package の同意は、使い方を制限する license（特定の hardware だけ、逆解析の禁止など）を受け入れてもらうための物。Apache-2.0 は使用に条件を付けず、再配布の条件（§4: LICENSE の写し、変更の表示、著作権表示の保持）は LICENSE と NOTICE を重みの横に install する事で満たせる。production の firmware の package は全て `LICENSE_PROMPT := y` なので、`n` は production で初めての例になる。Makefile と `userland/firmware/README.md` にこの理由を書く。

### 2.5 license の監査の覚え

- Apache-2.0 §4: (a) LICENSE の写し → 取得した原文を install。(b) 変えた file に変えた事を目立つように → NOTICE と manifest に「BN を畳んだ、stem の入力 channel を RGB の順に並べ替えた、形式を ZNET に変えた、他の値は変えていない」と書き、ZNET の header に元の SHA-256 を入れる。(c) 著作権表示 → NOTICE に「YOLOX, Copyright (c) 2021-2022 Megvii Inc., Apache License 2.0」。(d) 元に NOTICE が無いので引き継ぐ物は無い。
- F21: NOTICE に COCO の 1 行も足す（「The weights were trained by Megvii on the COCO 2017 dataset; COCO annotations © COCO Consortium, CC BY 4.0」の趣旨）。D4 の解釈がどちらでも条件を満たす。
- library・変換の script・shader は zedBSD の code（Zlib）。YOLOX の Python の code は写さない（構造は事実として読み、名前・書き方は自分で書く）。graph の層の名前（`dark3.1.m.0` など）は checkpoint の key と対応を取るための data として使う。
- COCO: zedBSD は COCO の画像も注釈も配らない（重みだけ）。試験の画像（§4）は COCO val2017 の license 7（No known copyright restrictions）か 8（United States Government Work）の物だけを選び、image id・Flickr の URL・license を試験の data の README に書く。

### 2.6 build の host の要件

- この package を選んだ時だけ、build の host に python3（3.8 以上、標準 library だけ）が要る。top の Makefile は既に `PYTHON ?= python3` を持ち、menuconfig と arch の overlay の image の作成が使う。`docs/howto/build-from-source.md` の「Python 3 for the interactive `make menuconfig` frontend」を「…と、任意の `yolox-nano-weights` の package（重みの変換）」に直す（p002、docs の変更は Q1 が merge）。
- 変換の時間は host で数秒（約 90 万の値の float64 の loop）の見込み（p002 で測る）。torch・numpy は要らない。

## 3. library `libyolox`

### 3.1 場所と build

- `userland/desktop/libyolox/`（D3）、共有 library `/lib/libyolox.so`、公開 header `yolox.h`、`exports.map` で `yolox_*` だけを出す。arch は amd64（QEMU・host の照合）と rpi5（rpi4 は WS283 の 4.2 が通れば足す）。
- 依存は libc・libm・dlopen だけ。libvulkan は link せず libmedia の `vkvideo-runtime.c` と同じく `dlopen("libvulkan.so.1")`、無ければ `libvulkan.so` → `vkGetInstanceProcAddr` から関数を引く。Vulkan が無い・compute が無い時も CPU で動く。
- Linux の host の build（`Makefile.linux`、`KEILAND_LINUX_LIBRARY`）を持ち、host の試験（§4）は host の cc と host の Vulkan の header・loader（lavapipe）で行う。
- shader は木の慣習に合わせる: `shaders/*.comp` と `shaders/regenerate.py`（`glslc --target-env=vulkan1.0 -O` と `spirv-val`）で作った `shaders.h` を commit し、build は shader の compiler を要しない。toolchain は変えない。

### 3.2 API（`yolox.h`、第 2 版）

```c
struct yolox_model;   /* 読んだ ZNET。device に依らない、不変、thread の間で共有してよい */
struct yolox_runner;  /* 1 つの backend と入力の大きさの実行の器。1 つの thread から使う */

enum yolox_backend { YOLOX_BACKEND_AUTO, YOLOX_BACKEND_VULKAN, YOLOX_BACKEND_CPU };
enum yolox_pixel_format { YOLOX_PIXEL_RGB8, YOLOX_PIXEL_RGBA8, YOLOX_PIXEL_BGRA8 };

#define YOLOX_RUNNER_DEBUG 0x1u  /* 全ての tensor を残す（§3.5）。試験用 */

struct yolox_image {
	enum yolox_pixel_format format;
	uint32_t width, height;     /* 1〜16384 */
	uint32_t row_bytes;         /* ≥ width × 画素の byte */
};

struct yolox_detection {
	float x1, y1, x2, y2;       /* 入力の画像の座標（切り詰めない） */
	float score;                /* obj × class */
	uint32_t class_index;       /* 0〜79、yolox_class_name() */
};

struct yolox_runner_config {
	uint32_t struct_size;       /* sizeof(struct yolox_runner_config)。ABI の版 */
	uint32_t flags;             /* YOLOX_RUNNER_DEBUG */
	enum yolox_backend backend;
	uint32_t input_size;        /* 64〜1024 の 32 の倍数、0 は model の既定（416） */
	float score_threshold;      /* 既定 0.3 */
	float nms_threshold;        /* 既定 0.3 */
	int class_aware;            /* 0 = class を区別しない（既定） */
	uint32_t max_dispatch_macs; /* 1 dispatch の MAC の上限、0 は既定（8,000,000、§3.6） */
	/* 任意: app の Vulkan の device を共有する（WS285 の debayer と同じ device）。
	   device が NULL なら library が自分で instance と device を作る */
	PFN_vkGetInstanceProcAddr get_instance_proc_addr;
	VkInstance instance; VkPhysicalDevice physical_device; VkDevice device;
	uint32_t queue_family; VkQueue queue;
	/* 任意: 共有の queue の外部の排他。NULL なら app が排他を保証する */
	void (*queue_lock)(void *context);
	void (*queue_unlock)(void *context);
	void *queue_context;
};

int yolox_model_load(const char *path, struct yolox_model **model);
void yolox_model_free(struct yolox_model *model);
const char *yolox_class_name(uint32_t class_index);
const char *yolox_reason(int code);

void yolox_runner_config_default(struct yolox_runner_config *config); /* struct_size と既定値を入れる */
int yolox_runner_create(const struct yolox_model *model, const struct yolox_runner_config *config, struct yolox_runner **runner);
enum yolox_backend yolox_runner_backend(const struct yolox_runner *runner);
void yolox_runner_free(struct yolox_runner *runner);

/* 同期: host の画像から検出 */
int yolox_detect(struct yolox_runner *runner, const struct yolox_image *image, const void *pixels,
	struct yolox_detection *detections, uint32_t capacity, uint32_t *count);

/* 非同期（Vulkan で device を共有した runner だけ）: device の VkBuffer の画像を入力に submit し、後で結果を取る */
int yolox_submit_buffer(struct yolox_runner *runner, const struct yolox_image *image,
	VkBuffer buffer, VkDeviceSize offset, VkSemaphore wait);
int yolox_collect(struct yolox_runner *runner, uint64_t timeout_ns,
	struct yolox_detection *detections, uint32_t capacity, uint32_t *count);

/* 試験・調べ: 最後の実行の tensor（ZNET の tensor の番号）を NCHW の fp32 で host に写す */
int yolox_runner_read_tensor(struct yolox_runner *runner, uint32_t tensor, float *values, size_t count);
```

約束（F12）:

- 戻り値は 0 か負の番号（`yolox_reason` で文）。`YOLOX_ERROR_TIMEOUT` は collect が時間内に終わらなかった事で、runner は in-flight のまま（もう一度 collect できる）。
- `struct_size`: library は min(struct_size, 自分の大きさ) だけを読み、足りない field は既定値。struct_size が第 1 版の大きさより小さければ `YOLOX_ERROR_ARGUMENT`。
- in-flight は 1 つ。collect の前の次の submit・detect は `YOLOX_ERROR_BUSY`。WS285 が 2 つ重ねたい時は runner を 2 つ作る（model は共有、device memory の重みは runner ごと）。
- `count` には NMS の後の検出の総数を返し、score の高い順に min(count, capacity) 個を書く。capacity が 0 の時は数だけ。
- `yolox_submit_buffer`: runner が app の device で作られた時だけ。library が自分の device を作った runner では `YOLOX_ERROR_NOT_SHARED`（別の device の VkBuffer は渡せない）。buffer は runner の queue family から読める事（CONCURRENT か、その family が持つ EXCLUSIVE）。所有権の移転は app の仕事（library は acquire をしない）。buffer は [offset, offset + (height − 1) × row_bytes + width × 画素の byte) を持つ事（library は buffer の大きさを知らない）。usage に STORAGE_BUFFER が要る。
- `wait`: binary semaphore（または VK_NULL_HANDLE）。library は COMPUTE_SHADER の stage で待つ。signal の前の app の書き込みは semaphore の決まりで見える。
- queue: library は `vkQueueSubmit` を `yolox_detect` と `yolox_submit_buffer` の中だけで呼び、`queue_lock`／`queue_unlock` が与えられればその間だけ呼ぶ。与えられなければ、app がその間に同じ queue で submit・wait idle しない事を保証する。collect は fence を待つだけで queue を使わない。
- `VK_ERROR_DEVICE_LOST`: runner は lost の状態になり、以後の呼び出しは全て `YOLOX_ERROR_DEVICE_LOST`。app は runner を free して作り直す（device を共有していれば device も）。
- 入力は VkBuffer だけ（D7）。
- `yolox_runner_read_tensor` は `YOLOX_RUNNER_DEBUG` の runner だけ。融合で存在しない tensor は `YOLOX_ERROR_FUSED`（§3.5）。

### 3.3 中の作り

- `model.c`: ZNET の読み込みと確かめ（§2.3）、重みは host の fp32 の配列（fp16 の file はここで fp32 に戻す）。
- `graph.c`: op の列から実行の計画を作る: tensor の形（入力の大きさから）、融合、dispatch の分割（§3.6）、memory の割り当て（§3.5）。backend に依らない。
- 融合（GPU の計画。CPU の参照は ZNET の op をそのまま実行し、融合しない）:
  1. **Focus＋stem は元の画像に対する 6×6・stride 2・pad 2 の conv**（F4）。出力 (oy, ox) は元の行 2oy−2〜2oy+3、列 2ox−2〜2ox+3 を読む。重み W6[o][i][j][c] = W3[o][p×3 + c][i >> 1][j >> 1]、p = (i & 1) + 2 × (j & 1)（左上 0・左下 1・右上 2・右下 3 の順と一致）。Focus の段の 0 の pad は元の画像の外の 0 と同じ。MAC は 6×6×3 = 3×3×12 で同じ。
  2. 残差の add は直前の pointwise の conv の後処理: `out = SiLU(acc + b) + residual`（§1.5）。add の前の tensor は残らない。
  3. concat は割り当てで消す（作る側が concat の buffer の channel の位置に直に書く）。
  4. upsample は concat の buffer の位置に書く小さな dispatch。
  5. maxpool 9・13 は 5 の重ね（§1.5）。
  6. head の reg と obj は 1 つの 1×1（出力 8 channel、5 を使う）。
- `cpu.c`: CPU の参照（§3.8）。`vulkan.c`・`vulkan-runtime.c`: Vulkan の backend（§3.6）。`decode.c`: decode の後半（閾値・÷r）と NMS（CPU、両方の backend で共通）。`preprocess.c`: §1.7 の大きさと係数の表、CPU の前処理。`classes.c`: COCO 80 の名前。

### 3.4 tensor の配置（C4 flat）

- GPU: 「C4 flat」= channel を 4 つずつ束ね、⌈C/4⌉ 個の block、各 block は HWa 画素の vec4（16 byte）。HWa = H×W を 32 に切り上げた数（1×1 の conv が lane あたり 2 画素まで guard 無しで扱えるため。13×13 の 169 は 192、26×26 の 676 は 704、他は余り 0〜16）。画素 (x, y) は block の中の y × W + x。余りの画素の値は不定で、空間の op（depthwise・maxpool・upsample・stem・decode）は読まない。
- 全ての concat の部分の channel は 16 の倍数なので、concat の部分は連続した block の区間になる。入力の 3 channel は RGB0 の 1 block（4 番目は 0）。head の cls 80 は 20 block、reg＋obj は 2 block（8 channel、5 を使う）。
- 重みは runner の作成の時に並べ替える（読む順に連続させ、ldunifa で流し読みできる形、§3.6）: 1×1 は [出力の block の組][ci/4][組の中の block][4×4]、depthwise は [c/4][9＋1]（9 tap と bias の vec4）、stem は [co/4][36 tap][3][4]（RGB の 3 入力 × 出力 4、4 番目の入力は読まない）、bias は各 block の後ろ。
- CPU の参照は NCHW の素直な配置（GPU と違う code・配置にして、照合の基準として独立させる）。

### 3.5 memory の計画（F13 の数を直した）

- 1 つの VkBuffer（arena）の中に、tensor の生きる区間（作る dispatch から最後に読む dispatch まで）で first-fit の offset を割り当てる（256 byte 境界）。concat の部分は concat の buffer の一部。
- 416 の fp32、C4 flat で: 生きている buffer の和の最大は **5.28 MiB**（入力の RGB0 2.64 MiB と stem の出力 2.64 MiB が同時に生きる所）、first-fit の arena も 5.28 MiB（`graph.py 416 gpu`）。320 では 3.12 MiB。第 1 版の「concat の別名で 6.60 → 4.62 MiB」は graph.py の数え方の誤り（別名の無い方だけ入力の画像を最後まで生かした）で、concat の別名は最大を減らさない。別名の利点は 13 個の写しの dispatch と、その帯域（読み書き約 15 MB）を無くす事。
- 他の buffer: 重み（C4 に詰めて 3.45 MiB、`readonly`）、decode の出力（3549 × 24 byte = 85 KB、host から見える）、前処理の係数の表（数 KB、host から見える）、host の入力の staging（host の画像の大きさ、変わった時だけ作り直す。1080p の RGBA で 8.3 MB）。device memory は 416 で約 9 MiB＋staging。
- 上限: S = 1024 で arena は約 32 MiB、最大の tensor は 16 MiB、group の数は最大 16384。runner の作成で device の limit と比べる（§2.3）。
- **debug の runner**（F7）: 同じ融合の計画を使い、arena の再利用だけをしない（全ての buffer の生きる区間を実行の全体にする。416 で約 38 MiB）。`read_tensor` は C4 flat から NCHW に直して返す。融合で存在しない tensor（Focus の出力、add の前の pconv の出力）は `YOLOX_ERROR_FUSED`。reg と obj は融合した 8 channel の tensor の 0〜3 と 4 として返す。CPU の debug の runner は全ての tensor を持つ。

### 3.6 Vulkan の backend

**shader と variant**（F1: specialization constant を使わない。zedBSD の前段は `OpSpecConstant*` を断る）。違いは build の時の variant（`regenerate.py` が `glslc -D…` で別の SPIR-V を作り、`shaders.h` に並べる）か push constant にする。memory の access の有無が変わる違い（残差の読み）は必ず variant にする（uniform な分岐でも if 変換で述語付きの load になるため）。活性化の有無は push constant（値の select だけ）。

| shader | variant | 仕事 | 1 invocation |
| --- | --- | --- | --- |
| `preprocess.comp` | 1 | 入力の画像（RGB8/RGBA8/BGRA8 は push constant、32 bit の語を 2 つ読んで byte を取り出す）→ §1.7 の letterbox（係数の表、整数）→ RGB0 の C4 | 1 画素 |
| `stem.comp` | 1 | 6×6 stride 2 の conv（3→16）＋ SiLU。tap は展開（loop 無し） | 1 画素 × 出力 1 block |
| `conv1x1.comp` | O ∈ {1, 2} × P ∈ {1, 2} × 残差 {無, 有} = 8 | C4 の 1×1。ci/4 の loop（回数は push constant）、出力 O block × P 画素。後処理: bias、SiLU（push）、残差（variant）、出力の block の位置（concat、push の offset） | O block × P 画素 |
| `dwconv3x3.comp` | stride 1・2 = 2 | depthwise 3×3、SiLU。tap は展開 | 1 画素 × 1 block |
| `maxpool5.comp` | 1 | 5×5、座標を clamp（§1.5） | 1 画素 × 1 block |
| `upsample2x.comp` | 1 | nearest ×2 を concat の位置へ | 1 画素 × 1 block |
| `decode.comp` | 1 | level ごと: cls の logit の最大（80、展開）→ sigmoid、obj の sigmoid、box の decode（network の座標）。{x1, y1, x2, y2, score, class} を書く（閾値で切らない） | 1 anchor |

計 15 の SPIR-V。

**invocation と dispatch の対応**（F8、WS283 の制約に合わせる）:

- local size は (16, 1, 1)（1 workgroup = V3D の 1 batch の 16 lane、supergroup は 1）。lane は画素。
- 1×1: group の格子は x = 出力の block の組（⌈Cout/4/O⌉）、y = 画素の tile（HWa / (16P)）、z = 1。**x（出力の組）を一番速く回す**ので、同時に動く workgroup が同じ画素の tile の入力を読み、入力は L1T・L2T に当たる見込み（CSD が x を先に回すのは推測、WS283 p007 の R5 で確かめる）。出力の組は `WorkgroupId.x` から決まり、全 lane で同じなので、重みの address は uniform（ldunifa を使える、WS283 §1.3。重みの binding は `readonly`）。lane l の画素は tile × 16P + p × 16 + l（16 lane の access が連続の 256 byte）。
- 空間の op（stem・depthwise・maxpool・upsample・decode・preprocess）: x = 行の中の 16 画素の tile（⌈W/16⌉）、y = 行、z = channel の block。行の終わりの tile で x ≥ W の lane は store を述語で止める（tile の数は ⌈W/16⌉ なので、最後の tile にも有効な lane が必ず 1 つ以上ある。WS283 の review M8 の「全 lane が無効の lookup」を起こさない）。load は全て座標を clamp した address で、述語を付けない。
- **整数の除算を使わない**（WS283 の review M7: QPU に mul-high が無い）。座標は全て group と lane の ID から直に作り、乗算・shift・and だけを使う。
- **dispatch の分割**（F3）: 1 dispatch の MAC が `max_dispatch_macs`（既定 8 M）を越える op は、画素の tile（1×1）か行（stem）の範囲で N 個の dispatch に分ける（範囲の始まりは push constant）。8 M MAC は効率 10%（5.5 G MAC/s）で約 1.5 ms、WS283 §2.3 の「1 dispatch を数 ms に」に収まる。

| | 416 | 320 |
| --- | --- | --- |
| 融合した dispatch | 119（前処理 1、stem 1、depthwise 30、1×1 79〈うち残差 7〉、maxpool 3、upsample 2、decode 3） | 119 |
| 8 M MAC で分けた後 | **149**（stem 10、dark5.1.conv2 4、他の大きな 1×1 が 2） | 129 |
| 実行する MAC（block・HWa の切り上げを含む） | 544.0 M（有用 522.6 M、+4%） | 330.0 M |

**command buffer**（F2）:

- 固定の graph の command buffer G は runner の作成の時に 1 回だけ記録する。G が bind するのは library が持つ buffer だけ（arena・重み・decode の出力・係数の表を 1 つの descriptor set に。tensor の位置は push constant の語の offset）なので、submit ごとに変わる物が無い。
- 入力の段の command buffer I は submit ごとに記録し直す（pool は RESET_COMMAND_BUFFER）。I は入力の descriptor set（入力の buffer の binding 1 つ）を bind し、前処理を dispatch し、compute→compute の barrier を置く。in-flight は 1 つなので、入力の descriptor set の更新と I の記録は前の fence を待った後（runner が idle の時）だけに行い、pending の command buffer の set を更新しない。
- 1 回の `vkQueueSubmit` に I と G を順に入れ、wait semaphore（COMPUTE_SHADER の stage）と fence を付ける。
- offset: descriptor の offset は offset を `minStorageBufferOffsetAlignment` の倍数に切り下げた値、range は残りの byte＋画像の byte を 4 に切り上げた値（`maxStorageBufferRange` 以下を確かめる）。切り下げた残りの byte は push constant で shader に渡す（shader は 32 bit の語で読んで byte を取り出すので、任意の byte の offset を扱える）。
- host の経路（`yolox_detect`）: 画像を library の host から見える staging の buffer に写し（HOST_COHERENT でなければ flush）、同じ入力の段を staging を入力にして記録する。staging は画像の byte が今の大きさを越えた時だけ、idle の時に作り直す。係数の表も大きさ（w, h）が変わった時だけ idle の時に書く。

**barrier**（F16）: 連続する全ての dispatch の間に global の memory barrier（COMPUTE→COMPUTE、SHADER_WRITE → SHADER_READ | SHADER_WRITE）を置く。arena の再利用で graph の上は独立な dispatch（cls と reg の枝）の間にも WAR・WAW があるため。V3D の実行器は job を 1 つずつ完了まで待つので費用は変わらない。最後に COMPUTE→HOST（HOST_READ）。

結果: host が 85 KB の記録を読み、CPU で閾値・÷r・NMS（`decode.c`）。GPU で NMS はしない。shared memory・barrier()・subgroup・atomic は使わない（V3D の shared は DRAM の上で速くならない、WS283 §1.4）。

### 3.7 WS283（bcm2711 の compute）との約束（F1・F3・F8・F9）

shader が使う物（Vulkan 1.0 の core、zedBSD の libvulkan の範囲）:

- storage buffer（std430、重みは `readonly`）、push constant ≤ 128 byte（1 dispatch で 12 語程度）、`WorkGroupID`・`LocalInvocationID`（local size (16, 1, 1)）、GLSL.std.450 の `Exp`、整数の加減算・乗算（IMUL）・shift・and・比較、float の min・max・floor、uniform な回数（push constant）の loop（1×1 の ci の loop だけ。他は展開）、述語付きの store（有効な lane が 1 つ以上）。
- 使わない: specialization constant、shared memory、barrier()、subgroup、atomic、16・8 bit の storage、image・sampler、整数の除算・剰余、述語付きの load。

WS283 の compiler の制約（WS283 design §1.3・§1.5、WS283 の review M1）と register:

- IR は scalar。vec4 の読みは 4 回の TMU の lookup（vector の TMU は WS283 p008）。各 access に範囲の clamp が付く（umin 1 つ程度、WS283 review H5）。TMU の FIFO は thread あたり 16 / threads 語。
- spill は無い。expression の register は 4 thread で **24 個**（rf4〜rf27、`shader-private.h` の `QUAD_REGISTER_END 28`。依頼文の 28 は rf の番号の上限）、2 thread で 56 個。足りなければ 2 thread に落ち、それでも足りなければ断られる。
- 1×1 の variant ごとの同時に生きる値の見込み（和 4·O·P、入力 4·P、重み 1〈ldunifa で流し読み〉、address と counter 3〜4。括弧は loop の変数の MOVE が合併されない時〈WS283 review M1〉に和が 2 重になる場合）:

| O × P | 生きる値 | 見込みの thread | MAC / 入力の lookup | MAC / 重みの語 |
| --- | --- | --- | --- | --- |
| 1 × 1 | 12（16） | 4 | 4 | 1 |
| 2 × 1 | 16（24） | 4（合併が無ければ 2） | 8 | 1 |
| 1 × 2 | 21（29） | 2 | 4 | 2 |
| 2 × 2 | 29（45） | 2 | 8 | 2 |

  第 1 版の「出力 4 × 画素 4」（約 48 個）はやめた。既定の計画は 2×1（WS283 p004 が MOVE を合併しなければ 1×1）で、p006 で層ごとに測って選ぶ。
- vec4 の TMU と ldunifa（WS283 p008）と compute の scheduler（WS283 review H2）が無い間は §5 の効率は出ない（§5 の「最初」の行）。
- 1 dispatch の長さ: §3.6 の分割で 8 M MAC 以下。
- 確かめ（F9）: **p008**（新しい Phase）で、WS284 の 15 の SPIR-V を全て host で共有の前段と bcm2711 の 7.1 の後段にかけて compile し（WS279 の `shader71-host-test.sh` の形）、断られない事と thread の数・register の数・命令の数を記録する。loop の無い shader は WS283 p003 の後、`conv1x1` は WS283 p004 の後に通る。WS283 の lane の model が compute を走らせられれば（WS283 H2）、T3 と同じ乱数の入力で CPU の参照と比べる。spec constant（F1）と register（F8-3）の失敗を Pi 無しで見つける。

### 3.8 CPU の参照

- C の素直な loop（NCHW、fp32、op ごとの関数: conv（k・stride・groups の一般形）、maxpool の一般形（5・9・13 を直に、GPU の重ねと独立に）、upsample、add、concat の写し、space-to-depth）。ZNET の op をそのまま実行する（融合しない）。前処理は `preprocess.c`、decode・NMS は `decode.c` を共有。
- 二つの役: (1) 照合の基準（PyTorch の参照の結果と比べ、GPU の各 op の試験の期待値を作る）、(2) compute が無い所での実行（Pi 5 の A76 で -O2 の自動 vector 化で 1 frame 100〜300 ms の見込み）。NEON の手書き・thread は範囲の外（D9）。

### 3.9 精度

- 既定は全て fp32（重み・活性化・計算）。
- 任意（p006）: 活性化と重みを `packHalf2x16` で fp16 に詰めて memory の量を半分にする（計算は fp32）。memory の帯域が律速と分かった時だけ。WS283 p008 の fp16 の pack・unpack が要る。
- 配る file は fp32 だけ（D8）。

## 4. 試験

host の試験は担当、QEMU・実機は T1 の担当に依頼する（AGENTS.md）。Vulkan の host の試験（T3〜T6、T9）は全て:

- **lavapipe に固定**: `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json`（host の loader 1.4.309）。試験の program は物理 device の `deviceType == CPU` と driver の名前が llvmpipe である事を確かめ、違えば FAIL（F18。host の Intel・他の GPU で動かない）。
- **validation layer 付き**: `VK_LAYER_KHRONOS_validation`（host に無い。p004 で Debian の `vulkan-validationlayers` を sudo で入れる）を有効にし、validation の error が 0 である事を合格の条件に入れる（F2）。

| 番号 | 何を | どこで | 基準 |
| --- | --- | --- | --- |
| T0 | 変換の script: (a) 2 回の出力の SHA-256 が同じ、(b) Python の読み手で読み戻して畳んだ値（float64 で計算した期待）と一致、(c) stem の RGB の並べ替え、(d) **畳んだ重みを ONNX の initializer と比べる**（F6。並べ替えの前の BGR の順で、Conv の node の順に対応を取る、`\|a − b\| ≤ 1e-6 × max(\|b\|, 1e-3 × 層の max\|b\|)`）、(e) build の固定の SHA-256 と一致 | host（Python、torch 不要。ONNX は試験の時に取得） | 一致 |
| T1 | 参照の結果を作る（1 回）: host の venv（`build/ws284/venv/`）で `exp.get_model()`＋checkpoint＋eval、`ValTransform(legacy=False)`。(a) `model.head.decode_in_inference = False` の出力（3549×85: reg は生、obj と cls は sigmoid の後）を ONNX（onnxruntime）の出力と比べる、(b) decode した検出（demo の閾値と評価の閾値の 2 組）、(c) 416 と 320 の両方 | host | (a) `\|d\| ≤ 1e-4 + 1e-4·\|ref\|` |
| T2 | CPU の参照 vs PyTorch: T1 の前処理の済んだ入力から、85 値と検出（416・320） | host | 85 値が `\|d\| ≤ 1e-3 + 1e-3·\|ref\|`。検出の集合が同じ（class が同じ、IoU ≥ 0.99、score の差 ≤ 1e-3）。除外: score が閾値から 2e-3 以内、NMS の候補の組の IoU が閾値から 2e-3 以内、score の差 1e-6 以内の同点（F19） |
| T2b | ZNET の load の負の試験（§2.3 の一覧）と API の引数の誤り | host | 全て決めた error を返し、crash しない |
| T3 | GPU の op ごと: 乱数の tensor（seed 固定）で 15 の SPIR-V の各形（stride・残差・concat の位置・活性化・O と P・分割の境・行の終わりの tile）を CPU の参照の op と比べる | host の lavapipe | `\|d\| ≤ 1e-4 + 1e-4·\|ref\|`（F6） |
| T4 | GPU の全体 vs CPU の参照: 同じ入力で 85 値（debug の runner の head の tensor）と検出、416・320。debug の runner で層ごとの比べ（存在する tensor だけ） | host の lavapipe | T2 と同じ |
| T5 | 前処理: GPU と CPU の C 版、C 版と cv2（T1 の venv）。画像の大きさは §1.7 の 415 になる高さ（23・45・90）、縦長・横長、拡大（S より小さい画像）、3 つの format、row_bytes に pad のある画像 | host（lavapipe・venv） | GPU = CPU（bit で一致）。cv2 とは 0〜255 の尺度で各値 ±1 以内、検出の集合は T2 の基準を IoU ≥ 0.9 に緩める |
| T6 | fp16 の詰め（使う時だけ） | host の lavapipe | 検出の集合（IoU ≥ 0.95、score の差 ≤ 0.02） |
| T7 | Pi 5 の実機: T3・T4 と速さ（dispatch ごとと全体、10 回の中央値）。zedBSD の image に libyolox・重み・`yolox-test` を入れる | Pi 5（WS283 の後、T1 の担当） | 85 値が `\|d\| ≤ 5e-3 + 5e-3·\|ref\|`（V3D の SFU の exp、R3）、検出は T2 の基準 |
| T8 | Pi 4・amd64 の QEMU（Venus）: 任意 | T1 の担当 | T7 と同じ |
| T9 | API（F18）: 非同期の submit・collect・timeout（0 と有限）、collect の前の二重の submit（BUSY）、自分の device の runner への submit_buffer（NOT_SHARED）、app が作った device と queue の共有（queue_lock の呼ばれ方）、offset が alignment の倍数でない buffer、capacity が count より小さい時、struct_size の古い config | host の lavapipe | 決めた通り、validation の error 0 |

venv（F17）: torch（CPU 版）・torchvision・opencv-python-headless・numpy・loguru・psutil・tabulate・packaging・onnxruntime、YOLOX の `requirements.txt` で import に要る物（足りなければ import の失敗を見て足す）。YOLOX は commit 6ddff482 を PYTHONPATH に置き、C の拡張は build しない。版は p002 で pin し、`pip freeze` を参照の data と一緒に記録する（cv2 の resize と torch の数値は版で少し変わる）。venv は build・image に使わない。

試験の data（D5、`plan/ws284/tests/data/`）: COCO val2017 の license 7・8 の画像 3 枚。各画像について、元の JPEG（T5 用）、T1 の前処理の済んだ 416×416 と 320×320 の RGB（binary PPM）、参照の検出（文、2 組の閾値）、85 値の一部（7 個おきの anchor と score の上位 100、fp32）。合計約 3.5 MB。全 tensor の dump は `build/ws284/ref/` に作り commit しない。**回帰の data として WS の後も使うので、p007 で tests/ のシナリオ（または Master の Tools に登録する場所）へ移す**（F20、WS の完了で `plan/ws284/` が消えるため）。

## 5. Pi 5 の見積もり（F8: 1.15 GHz の観測と WS283 の制約で書き直した）

前提: V3D 7.1.10、12 QPU、**1.15 GHz**（WS279 p002 の実機）。QPU は 16 lane を 4 clock で 1 命令、add と mul の ALU を 1 つずつ（FMA は無い）。1 MAC = fmul＋fadd が 1 語に対になれば、最大 12 × 16 / 4 × 1.15 G = **55.2 G MAC/s**（WS283 §3.3 の 110 GFLOPS と同じ）。memory は LPDDR4X、TMU の実効の帯域は不明（WS283 に倣い 5〜10 GB/s と置く）。

DRAM の量（`graph.py 416 gpu`、fp32、C4 flat）:

| 量 | 416 | 320 |
| --- | --- | --- |
| 書き | 39.7 MB | 23.7 MB |
| 読み（各入力を 1 回、重み 3.6 MB を 1 回） | 54.5 MB | 33.9 MB |
| 下限の計（cache が入力の tile の再利用を全て拾う時） | **94 MB** | 58 MB |
| 上限の計（1×1 が出力の組ごとに入力を DRAM から読み直し〈471 MB〉、重みを 16 画素の batch ごとに読み直す〈114 MB〉時） | 625 MB | 380 MB |

dispatch の間の再利用は無い（WS279 の design: job の前に L2T を flush、後に clean）。1 dispatch の中は §3.6 の順（出力の組を一番速く）で下限に近づける見込み（推測、WS283 R5 で測る）。

| 項目 | 416（詰めた後） | 416（最初） | 320（詰めた後） |
| --- | --- | --- | --- |
| 実行する MAC | 544 M | 544 M | 330 M |
| 効率 | 15〜30%（WS283 §3.3、vec4 の TMU・ldunifa・compute の scheduler の後） | 3〜6%（scalar の TMU、重みも TMU、1 語 1 op） | 15〜30% |
| ALU の時間 | 33〜66 ms | 165〜330 ms | 20〜40 ms |
| memory の隠れない分（下限〜上限の一部） | 0〜20 ms | 0〜40 ms | 0〜12 ms |
| dispatch の固定の分（149 / 129 × 30〜80 µs、WS283 §3.3） | 4.5〜12 ms | 4.5〜12 ms | 4〜10 ms |
| 見込み（1 frame） | **40〜100 ms（10〜25 fps）** | **170〜380 ms（3〜6 fps）** | **25〜60 ms（17〜40 fps）** |

- 第 1 版（800 MHz、60〜150 ms）より速い見込みになったのは clock の訂正による。「詰めた後」は WS283 p008 と compute の scheduler（WS283 の review H2）に依る。
- 比べ: A76 ×4（2.4 GHz、NEON の FMA 4 幅 × 2）は最大 76.8 G MAC/s で、よく作った CPU の実装なら V3D より速い見込み（R1）。ユーザーの目標は Vulkan compute なので V3D を主にし、CPU の参照は fallback。
- fallback の順: (1) 入力 320（同じ重み、0.61 倍）、さらに 256。(2) 検出を N frame に 1 回にし、表示は camera の速さで前の box を出す（WS285）。(3) fp16 の詰め（§3.9）。(4) depthwise と pointwise の融合（DWConv の 2 dispatch を 1 つ）。(5) CPU の backend。
- WS285 の目標の案: 416 で 5 fps 以上、320 で 10 fps 以上（p006 の測定で見直す）。最初の段階（WS283 p008 の前）では 416 で 3〜6 fps の見込み。

## 6. Phase

| Phase | 内容 | 依存 | 見込み |
| --- | --- | --- | --- |
| p001 | 設計（この文書、第 2 版） | — | — |
| p002 | 重み: 変換の script（§2.2、`userland/firmware/yolox-nano/yolox-convert.py`）、ZNET の仕様の確定、package `yolox-nano-weights`（取得・確かめ・build の中の変換・固定の SHA-256・install、NOTICE、manifest、firmware の README・build の文書の 1 行）、T0（ONNX の initializer の照合を含む）、T1 の venv と参照の生成（416・320、decode 無しと有り）、試験の画像の選定と license の記録 | p001 | 5〜7 LW |
| p003 | libyolox の骨格と CPU の参照: `yolox.h`（第 2 版の約束）、model の読み込みと確かめ（§2.3）、graph の計画（形・融合・分割・割り当て）、CPU の op・前処理（§1.7）・decode・NMS（§1.6 の規則）・class 名、Linux の host の build、T2・T2b | p002 | 7〜9 LW |
| p004 | Vulkan の backend: dlopen・device の作成と共有・queue の排他・arena・重みの並べ替え・15 の SPIR-V と regenerate.py・入力の段と固定の graph の command buffer・分割・barrier・submit/collect・debug の runner、validation layer の導入、T3〜T6・T9（lavapipe） | p003 | 10〜13 LW |
| p005 | zedBSD の package: libyolox（amd64・rpi5）、重みの package の image への導入、試験の program `yolox-test`（`userland/tests/`）、試験の image の config.mk（`plan/ws284/tests/`）、T8（amd64 QEMU、任意）を T1 の担当に依頼 | p004 | 3〜4 LW |
| p006 | Pi 5: T7、dispatch ごとの時間、variant（O・P）と分割の上限の調整、必要なら fp16 の詰め・dw＋pw の融合、416/320 の推し | p005、p008、**WS283 p004・p006・p007 が cleared**（速さの判定は WS283 p008 の後） | 6〜10 LW |
| p007 | 規約の全文の見直し（coding-style.md）と license の監査（§2.5、shader・script を含む）、試験の data と回帰の試験を tests/ と Master の Tools へ移す（F20） | p006 | 2〜3 LW |
| p008 | V3D 7.1 の host の compile の確かめ（§3.7）: 15 の SPIR-V を共有の前段と bcm2711 7.1 の後段で compile、thread・register・命令の数の表、WS283 の lane の model があれば T3 の入力で照合 | p004、WS283 p004（loop の無い shader は WS283 p003 の後に先に流せる） | 2〜3 LW |

計 **35〜49 LW**（第 1 版 29〜41 LW）。増えた分: build の中の変換と package（p002 +1）、ZNET の確かめと負の試験・前処理の厳密化（p003 +1）、入力の段・variant・分割・debug・API の試験・validation layer（p004 +2〜3）、p008（+2〜3）。p002〜p004 は host だけで進められ、WS283 を待たない。

## 7. ユーザーの判断

決まった事（2026-10-23 ユーザー「decisionは推しでいいです。重みはダウンロードしてビルドする形にしましょう。それならソースツリーにライセンスを混入させないはずです。」）:

| 番号 | 決定 |
| --- | --- |
| D1 | 重みは build が公式の `yolox_nano.pth` を取得し（firmware の repository は同じ byte の mirror を後で置いてよい）、size と SHA-256 を確かめ、build の中で ZNET に変換する。変換した重みはどの repository にも置かない（§2） |
| D2 | package `yolox-nano-weights`、tree は `userland/firmware/yolox-nano/`、menu は Firmware の下。同意の問いは D10 |
| D3 | `libyolox`、`userland/desktop/libyolox/` |
| D4 | 重みの license は repository の Apache-2.0、COCO で学習した重みの配布を受け入れる（§2.5 の記録） |
| D5 | 試験の data は `plan/ws284/tests/data/`（p007 で tests/ へ移す、F20） |
| D6 | 既定の閾値は demo と同じ conf 0.3・NMS 0.3・class を区別しない |
| D7 | GPU の入力は VkBuffer だけ |
| D8 | fp32 の file だけ。fp16 は p006 で帯域が律速の時だけ活性化に |
| D9 | CPU は素直な C だけ |

新しく確かめたい事:

| 番号 | 問い | 推し |
| --- | --- | --- |
| D10 | image の build で、firmware の package のような license の yes/no の問いを出すか | **出さない**（§2.4）。Apache-2.0 は使用の同意を要せず、再配布の条件は LICENSE と NOTICE を重みの横に install して満たす。production の firmware の package で初めての `LICENSE_PROMPT n` になる事を Makefile と README に書く。代案: 他の firmware と揃えて問いを出す（費用は小さいが、同意が要らない物に同意を求める事になる） |
| D11 | 試験の画像（COCO val2017 の license 7・8 の 3 枚、約 1 MB の JPEG と前処理の済んだ PPM）を tree（`plan/ws284/tests/data/`、後に tests/）に入れてよいか。license 7 は「既知の著作権の制限が無い」、8 は米国政府の著作物で、license の文を伴わない | **入れる**（D5 の通り）。「ソースツリーにライセンスを混入させない」の趣旨には反しない見込み。代案: 画像も URL・size・SHA-256 で試験の時に取得し、tree には zedBSD が作った数値（参照の検出・85 値）だけを置く |

## 8. 危うさと未確認

| 番号 | 中身 | 対処 |
| --- | --- | --- |
| R1 | V3D が CPU（A76 ×4）より遅い見込みがある | p006 で測り、§5 の fallback。結果をユーザーに示す |
| R2 | dispatch ごとの CSD の job と L2T の clean の固定の時間（149 dispatch） | §3.6 の分割の上限を p006 で調整、WS283 R5 の測定。多ければ dw＋pw の融合 |
| R3 | V3D の SFU の exp の精度が lavapipe と違う | T7 の許容差を広げ（§4）、境の検出を除く |
| R4 | PyTorch の参照の venv（数百 MB）を host に入れる | `build/ws284/venv/` に閉じる。入らなければ onnxruntime と公式の ONNX で代える（T0 (d) で ONNX の重みが同じ事を確かめた） |
| R5 | build の中の変換が Python の版・host で違う byte を出す | IEEE の規定で起きない見込み。起きれば固定の SHA-256 で build が止まる（§2.2 の 5・6）。T0 (e) を 2 つの Python の版で流す（p002） |
| R6 | CSD の workgroup の回り方（x が先か）が推測と違い、1×1 の入力の再利用が効かない | WS283 R5 で測る。違えば格子の軸を入れ替える（shader の variant を変えずに push constant で軸を選べる形にしておく） |
| R7 | WS283 の register の数・MOVE の合併で 2×1 が 4 thread に収まらない | p008 で数を見て 1×1 に落とす |
| U1 | TMU の実効の帯域、L2T の大きさ | WS283 p007 の R5 |
| U2 | 前段・後段が `Exp`・IMUL・shift・uniform な loop・述語付きの store を通すか | p008 |

## 9. 他の WS への伝達（Q1 経由）

- WS283: (1) WS284 は specialization constant を使わない（WS283 §1.6 の「前段が既定値で畳む」は今の code と違う、WS283 review H1）。(2) §3.7 の使う物・使わない物の一覧。(3) 1×1 の variant の生きる値の表と、loop の変数の MOVE の合併の要望（2×1 を 4 thread に）。(4) 重みの binding は `readonly`、出力の組は `WorkgroupId.x`（uniform）なので ldunifa の対象（WS283 review H6 の条件）。(5) 1 dispatch は 8 M MAC 以下に分ける、416 で 149 dispatch。(6) CSD の workgroup の順（x が先か）を R5 で確かめてほしい。(7) 第 1 版の問い「同じ submit の中の CSD の job の間の clean を省けるか」は WS283 p008 の範囲で答えを待つ（WS283 review M6）。
- WS285（F22）: 学習の data は sRGB の JPEG（gamma・white balance の済んだ物）。debayer が ISP（AWB・CCM・gamma）無しの RAW から作る画像では精度が落ちる見込み（推測）。WS284 の「参照と一致」の基準ではこれは分からないので、WS285 の設計で簡単な AWB・gamma を考える。入力は §3.2 の `yolox_submit_buffer`（RGBA8/BGRA8 の VkBuffer、alignment は library が扱う）。

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
