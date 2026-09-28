<!-- awesome-plan project=zedbsd record=ws035p107 -->

# ws035-p107: Kei の全画面の起動画面と spinner（見た目の段階 2 の 1）

Phase ID: `ws035-p107`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て 4 の前半。[kei-identity-design.md](../kei-identity-design.md) の段階 2）

## 範囲

起動画面を `userland/desktop/artwork/kei-boot-splash.png`（tree に入れてよいとユーザーが許した画像）から作る全画面の絵にし、
UEFI・BIOS の loader に全画面の描き方を足す。kernel の quiet console の進みの表示を、絵の青い点の spinner にする（HAL の
`cons.c` は実装の変更だけ、hal.h は不変）。greeter・lock・壁紙・files は [p108](../phase108/phase.md)。

## 実装（2026-09-28）

- **絵**（`tools/build/make-boot-splash.py`、新。`tools/build/make-boot-logo.py` は使われなくなったので削除）: PNG を標準
  library だけで読み（zlib）、絵の spinner を周りの空と水から埋めて消し、1440x810（16:9）に bilinear で縮め、header に
  `# fit=cover` の comment を持つ PPM にする。`platform/amd64/vmunix.mk` の `$(BUILD)/boot-logo.ppm`（ESP と BIOS の FAT の
  `logo.ppm`）がこれを使う。約 3 秒。spinner の場所（高さの 77.2 %、輪の半径 3.08 %、点の半径 0.64 %）は script と kernel の
  `splash.c` の約束。
- **UEFI loader**（`bootloader/uefi/logo.c`）: header の comment `fit=cover` を読み、絵の比率を保って画面を覆うように bilinear
  で拡大縮小して描く（はみ出す分は両側から切り、中央を残す）。comment の無い PPM は従来どおり中央に。
- **BIOS loader**（`bootloader/bios/logo.c`・`logo.h`）: 同じ comment で、縮小になるときだけ最近傍で覆う（1 sector の書き込み
  が 172 に収まるため）。拡大になる画面では従来どおり中央（1440x810 を選んだのは BIOS の VBE の 1024x768 でも縮小にするため）。
  struct の末尾に 40 byte を足した（bootzbsd.S の知る offset は不変、`_Static_assert`）。
- **spinner**（`src/drivers/platform/pcat/graphics/splash.c`・`splash.h`、新）: 画面の大きさから絵の中の spinner の場所を出し、
  下の絵を保存して 8 つの点（先頭が濃い青、後ろほど淡い）を 16 標本の anti-alias で描く。`step` で 1 つ回る。
- **HAL**（`src/hal/amd64/bsp-pcat/cons.c`、実装だけ、hal.h は不変）: `kmsg=quiet` のとき右上の進捗の枠を、左隣の 1 色ではなく
  各列の真下の色で塗る（写真の空でも継ぎ目が見えない）。spinner を始め、早期 console が出さない行ごとに 1 つ回す。
- **kernel**: text-display の ops に任意の `progress(int end)`、`kern_text_progress()`・`kern_text_progress_end()`
  （`include/kern/text-display.h`・`src/kern/text-display.c`）。`klog.c` は quiet のとき record ごとに `kern_text_progress()`。
  `gpu.c` は表示の lease を取ったときに `kern_text_progress_end()`（GPU の画面の下の firmware の framebuffer に描き続けない）。
  pcat の text 層（`text.c`）: quiet の起動で spinner を自分の mapping へ移し、log の record・隠れた console の改行・125 ms ごとの
  kernel thread（最長 2 分）で回し、reveal・lease で止める（待ちの長い所でも止まって見えない）。
- 試験の道具（host）: `plan/ws035/tests/p107/logo-host.c`（UEFI の描き方を memory の framebuffer で）、`bios-logo-host.c`
  （BIOS の decoder に 512 byte ずつ）、`spinner-host.c`（spinner）。`build-login-image.sh` は第 3 引数以降に make の target。

## 検証（2026-09-28）

- host: logo-host で 640x480・1920x1080・2560x1080 を覆う（切り方と中央）。bios-logo-host で 640x480 は cover・sector の最大
  書き込み 114/172、UEFI の bilinear との差は平均 0.2 階調。spinner-host で位置が元の絵の spinner と一致（0 と 3 step）。
- QEMU（amd64、graphical-network の image `build/ws035-d3`、cfg は `logo=logo.ppm kmsg=quiet login=graphical`、GPU 無し）:
  - UEFI（OVMF、640x480）`boot-shots.py`: loader の全画面の絵（1.7 秒）→ spinner が回り続ける（1.7〜14 秒で 40 枚の違う画面）→
    GPU が無いので greeter が終わり console の login（`reveal` で絵が消える）。
  - BIOS（SeaBIOS、VBE 1024x768）`boot-shots.py --bios`: 絵が sector ごとに描かれ（約 2 秒）画面を覆う → spinner が回る → login。
  - boot test（`plan/tools/boot-test.sh`）PASS。
- 画面: `build/ws035-shots/p107-20260928-uefi-splash.png`・`-uefi-spinner.png`・`-uefi-console.png`・`-bios-drawing.png`・
  `-bios-spinner.png`・`-host-cover-1920x1080.png`・`-host-spinner-zoom.png`。
- build warning 0（変えた file）。style-check: splash.c・logo.c 2 つは 0、cons.c・text.c・klog.c・text-display.c・gpu.c は変更前と
  同じ数（既存）。
- 未実施: 実機。Venus・i915 の guest で greeter が表示を取った時の spinner の停止（`kern_text_progress_end`）は build だけで、
  画面の確認は無い（Venus は別の画面）。

## 残り

- BIOS の loader で絵を拡大する画面（1440x810 より大きい VBE）は中央に描く（覆わない）。BIOS は 3.5 MB を sector ごとに読む
  （QEMU で約 2 秒）。
- spinner の場所は build の script と `splash.c` の約束（絵を変えるときは両方）。
