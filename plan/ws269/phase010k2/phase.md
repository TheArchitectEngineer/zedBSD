<!-- awesome-plan project=zedbsd record=ws269-p010k2 -->

# ws269-p010k2: readback の道で vkdemo の画、gpu-selftest の `present`

- ID: ws269-p010k2（親 [ws269](../ws.md)）
- Status: planning（q1391-i01 で準備: host の llvmpipe で期待の画を決め、判定と UAT の文面を下に。実装は未）
- Disposition: normal
- 設計: [design-p010.md](../design-p010.md) §8.2・§10 の p010k2
- 依存: O-p010k1（WSI の readback、P2）、O-p010j（depth、vkdemo は D の attachment を使う）

## 期待の画（2026-10-20 10:53 P1、host の llvmpipe）

- 取り方: vkdemo を host で build（`cc -std=gnu11 -O1 -I. -Iinclude userland/tests/vkdemo/{main,renderer,display}.c userland/base/common/sha256.c -lvulkan -lm`、tree の外の `build/p010n/vkdemo-host`）し、`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json vkdemo --offscreen --time-ms=T --hold=0 --output=FILE.ppm` を T = 0・1000・2500 で（320×240、PPM の RGB）。
- 画: 暗い紺の背景に、色の市松の面の直方体（cuboid）が中央にある。
- 代表の pixel（RGB 16 進）:

| T（ms） | 中央 (160,120) | 四隅 (5,5)・(314,234) と (80,60)・(240,60)・(80,180)・(240,180)・(160,60)・(160,180) | `rgb_sha256` |
| --- | --- | --- | --- |
| 0 | e06895 | 101828 | 5a70a9cf68a7d7cd… |
| 1000 | 20c5b0 | 101828 | 2ef573c3be9f28b7… |
| 2500 | 206520 | 101828 | 5546eac6c25ab5bc… |

- 背景の 101828 は clear の値なので PowerVR でも完全に一致するはず（推測: clear は PBE の pack だけ）。直方体の中の色は rasterization と texture の filter で llvmpipe と違い得るので、sha256 の一致は求めない。

## gpu-selftest の `present` の判定（案、実装は p010k2 で）

1. 今（q1388）: `vkdemo --duration=4` の exit 0（`present` の need は `depth`）。
2. p010k2 で足す: `vkdemo --offscreen --time-ms=0 --hold=0 --output=/tmp/gpu-selftest-vkdemo.ppm` を先に流し、PPM を読んで (5,5)・(314,234) が 101828 ちょうど、(160,120) が e06895 から各 channel ±24 以内なら ok。外れたら `fail(pixel (x,y) RRGGBB, want RRGGBB)`。その後に表示の `--duration=4`。
3. 期待値は tree の中（gpu-selftest の source の定数）に置き、出所（この表、host の llvmpipe）を注記する。

## ユーザーの UAT の文面（案）

- 起動の後、画面の console に `gpu-selftest: present: ok` が出ること（`depth` が通らない間は `present: skip(depth)`）。
- `present` の 4 秒の間、画面の中央に色の市松の直方体が回って見えること（暗い紺の背景）。止まって見える・黒い・乱れた縞は fail として、どの段まで ok かの行と一緒に記録する。
- 見られない場合は gpu-selftest の行だけで判定する（上の 2 の pixel の判定）。

## 未実施

- PowerVR の実機での offscreen の画と表示（ユーザーの UAT 待ち）。
- 上の 2 の pixel の判定の実装（p010k1 の readback の道の後）。

## q1393-i01（2026-10-20 P1）: present の判定の実装

- `userland/tests/gpu-selftest/main.c`: `present` は先に `vkdemo --offscreen --time-ms=0 --hold=0 --output=FRAME`（既定 `/tmp/gpu-selftest-vkdemo.ppm`、`-f` で変える）を流し、PPM の (5,5)・(314,234) が 101828 ちょうど、(160,120) が e06895 から各 channel ±24 以内かを見て、外れれば `present: fail(pixel (x,y) RRGGBB, want RRGGBB)`、その後に `--duration=4` の表示。vkdemo に option の追加は要らなかった（`--offscreen`・`--time-ms`・`--hold`・`--output` は既にある）。
- 直し: gpu-selftest は子の出力の最初の 1 行だけ読んで pipe を閉じていたので、多く書く vkdemo が SIGPIPE で落ちていた（host の llvmpipe で再現: `fail(offscreen: VKDEMO START …)`）。残りを読み捨てる形にした。
- 確かめ: selftest-host.sh 0 failed（frame の stand-in、黒い frame で `fail(pixel (5,5) 000000, want 101828)` の場面を追加）。host の llvmpipe の vkdemo を `-d` に渡した gpu-selftest で `present: ok`。VF2 の userland の gpu-selftest の build rc=0、warning 0。実機は未確認。
