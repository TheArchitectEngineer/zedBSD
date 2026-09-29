<!-- awesome-plan project=zedbsd record=ws035p129 -->

# ws035-p129: compositor の起動を速くする（Venus で socket まで約 6.5 秒、login・Log Out の待ち）

Phase ID: `ws035-p129`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-29、サブエージェント、worktree `wt/ws035`。QEMU の Venus、実機は未実施）
Phase disposition: normal
Queue: なし（2026-09-29 main の割り当て「p116 の一覧の 8（Venus で compositor が GPU を開いてから socket を出すまで約 6.5 秒、login の
引き継ぎが遅い）の調査と短縮」）

## 調べ方

compositor（`/bin/wayland`）の起動の各段の時間を `ZWL STARTUP step=NAME ms=N [at_ms=T]` の診断の行で出すようにして（残す）、Venus の guest
（graphical の login の image、1920x1280）の session の log で読んだ。段: `gpu`（`/dev/gpu0`）、`compose`（その内訳 `vulkan-device`・
`vulkan-objects-arrow`・`wallpaper-picture`・`wallpaper`（blur を含む）・`glyphs`（内訳 `glyphs-text`・`glyphs-icons`・`glyphs-mark`））、`input`・
`keymap`・`socket`。session の GO の後の表示の取得は `output-display`・`output-swapchain`・`output-pipelines`・`output-targets`。

## 分かったこと（前、1920x1280）

| 段 | ms |
| --- | --- |
| vulkan-device | 193〜208 |
| vulkan-objects-arrow | 345〜349 |
| wallpaper（ppm の読みと変換 364〜380 + blur 約 294） | 652〜674 |
| glyphs（font の読み約 220 + 文字 27 + icon 106〜115 + **Kei の印 1415**） | 1586〜1774 |
| **compose の計** | **2793〜2973** |
| input・keymap・socket | 0〜1 |

**Kei の印の atlas（`atlas_mark`、`userland/desktop/artwork/mark.c` の `keiland_mark_raster`）が 1.4 秒**で最大だった。7 層 × 128 px と 26 px ×
1 pixel 16 標本で、標本ごとに 3 回の `sqrtf` を呼ぶ（約 570 万回）。userland は `-ffreestanding -fno-builtin` で build され、`sqrtf` は
C library の software の平方根（`src/libc/math/sqrt.c`、整数の演算で double の根）への call になり遅い。

GO の後の表示の取得（約 640 ms）は `output-swapchain` 434 ms（libvulkan の WSI: display の claim と blob の image の確保）・`output-pipelines`
約 120 ms・`output-targets` 約 90 ms。

## 実装（2026-09-29）

- `userland/desktop/artwork/mark.c`: 円の中の深さ（bar の角の丸み、leaf の 2 つの円）を `mark_circle_depth()` に集め、**平方根は edge の光
  （rim、`MARK_RIM_WIDTH`）の帯の中の点だけ**で取る。層は深さを符号（中か外か）と、rim の帯の中の値にしか使わないので、外は -1、帯より深い
  所は半径（真の深さ以上で帯の外）を返しても各層の値は同じ。**出力は変更前と bit ごとに同じ**（下の host の試験）。Files の Home の印
  （`ui-home.c`）も同じ関数で速くなる。
- `userland/desktop/wayland/main.c`・`compose.c`・`glass.c`: 上の `ZWL STARTUP` の診断の行（`startup_step()`、各段の ms）。

## 検証

**host**: `plan/ws035/tests/p129/run-mark-compare.sh acd160ac`（新、変更前の mark.c と今の mark.c で 7 層 × 7 つの大きさ（14・26・48・64・96・
128・200 px）の全 pixel を比べる）PASS（差 0）。

**QEMU（amd64、Venus の guest、graphical の login の image）**:

- 起動の段（1920x1280、`build/p129-after.img`）: `glyphs-mark` 1415 → **135〜156 ms**、`glyphs` 1586〜1774 → 480〜515、**compose の計
  2793〜2973 → 1704〜1742 ms**（約 1.2 秒短縮）。他の段は同じ（vulkan-device 194、objects-arrow 347、wallpaper 683）。
- login・Log Out の待ち（1280x800、`zdesktop-p126.sh` の 3 周、前 `build/p129-before.img`（main の acd160ac）・後 `build/p129-after.img`）:

| | 前 | 後 |
| --- | --- | --- |
| login: greeter の「Starting session...」から session の READY まで（sessiond の `session ready waited_ms`） | 2943・2831・2959・3021 ms | **1703・1614・1578・1528 ms** |
| Log Out: App Home の Log Out から新しい greeter の READY まで（`logout at_ms` → `greeter ready: waits at_ms`） | 2713・3098・2793 ms | **1509・1530・1639 ms** |
| 替わり目の保った画（release → 次の最初の frame、dmesg） | 1220〜1354 ms | 1219〜1373 ms（同じ、表示の取得の swapchain が主） |

  両方の run で黒 0・文字 console 0（p126 の試験 PASS）。
- 回帰: `zdesktop-p101.sh` PASS（1280x800）、`zdesktop-p128.sh` PASS（1920x1280、Files の Home の印を含む画面）。
- boot test: `plan/tools/boot-test.sh build/p129-after.img` PASS。
- build: `build-login-image.sh build/amd64 graphical`（clang・libcxx を含まない config）、warning 0。
- 規約: `plan/tools/style-check.py` main.c・compose.c・glass.c・mark.c・`mark-compare.c` 0（変更前も 0）。`git diff --check` 清浄。

未実施: 実機（i915）。p116 の 6.5 秒の測り方（lean な files の image で compositor を手で起こし socket を待つ試験）での再計測（同じ compose の
道で、session の計測で代えた）。

## 残り（範囲外、main へ）

- **userland の `sqrtf` が遅い**: `src/libc/math/sqrt.c` は software の平方根で、userland の `-ffreestanding -fno-builtin` のため全ての `sqrtf`・`sqrt`
  がそこへの call になる。amd64・arm64 の hardware の平方根（`sqrtsd`・`fsqrt`）を libm に使えば、icons（atan2f・sinf・cosf も）や他の描画も速くなる
  見込み。libc の持ち主の判断（WS035 の範囲外）。
- 替わり目の保った画（約 1.3 秒）の主は `output-swapchain` 434 ms（libvulkan の WSI の claim と image の確保）。compositor の側でできるのは
  pipelines（約 120 ms）を READY の前に作ること（format を先に知る必要がある）。libvulkan の側の短縮は libvulkan の持ち主と相談。
- wallpaper の読み（ppm 1920x1280 の読みと float への変換 約 370 ms）と blur（約 290 ms）はそのまま。

## Resume point

2026-09-29: cleared。次は ws.md の残りから人間の判断が要らないもの。
