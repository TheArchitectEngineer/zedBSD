<!-- awesome-plan project=zedbsd record=ws035p116 -->

# ws035-p116: デモの通しの確認（Venus、1920x1280）と見つけた粗の修正

Phase ID: `ws035-p116`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て「デモの仕上げ」3）

## 範囲

2026-10-17 のデモの流れを Venus の guest で通す: 起動 → greeter → login → App Home → Files → Terminal → 右上のスワイプで Notes →
PDF Viewer → Wiseview → lock → logout。デモの LCD の 1920x1280 で（harness が許せば）。WS035 の範囲の見た目の粗・ずれ・古い
「zedBSD」「zdesktop」の UI の文言を直し、範囲外は main への一覧にする。各段の画面を `build/ws035-shots/` に残す。

## 道具（2026-09-28）

- `plan/ws035/tests/zdesktop-guest.sh`: `VENUS_SIZE=WxH` で virtio-gpu の既定の mode（`xres`・`yres`）を変える（既定は QEMU の 1280x800）。
  Venus の driver は GET_DISPLAY_INFO の矩形を使うので、greeter も session も 1920x1280 で開く（`ZWL GPU ... width=1920 height=1280`）。
- `config-amd64-demo-venus.mk`・`build-demo-venus-image.sh`: graphical login の image（files・PDF Viewer・greeter）に Notes と demo の App Home の
  一覧（`plan/ws035/demo/apps.conf`、i915 の demo の image と同じ）。
- `demo-walk.sh`: 通しを 12 段で撮る（00 の splash は VGA の QMP screendump、他は Venus の VNC）。pointer は指・ペンのように押し・動き・離し。
  PDF は host の `/usr/share/doc/quilt/quilt.pdf`（`DEMO_PDF` で変更）を login の後に `/root/Documents` へ入れる。

## 見つけたこと（1920x1280）と直したもの

WS035 の範囲で直した（compositor、`userland/desktop/wayland`）:

1. **greeter・lock の Log In の button が裸の「>」**だった → U+2192 の矢印（glyph cache、無ければ fallback の font）（`greeter.c`）。
2. **greeter と session の開始の直後、pointer の矢印が画面の中央（greeter では avatar の上）に出ていた**（touch の demo では動かさない矢印が
   残る）→ pointer の device が位置を報告するまで矢印を描かない（`pointer_unmoved`、`main.c`・`input.c`・`compose.c`。同じ位置への絶対
   座標の報告でも表示し、その場所を damage する）。
3. main の追加の依頼（system bar の launcher を Kei の印に、「Kei」の語を外す）は [p117](../phase117/phase.md)。

直さず確認したもの（設計どおり）: App Home の右下に残る desktop の角（`HOME_KEEP` の 26 px、閉じる場所）、glass の title bar と本体の間の隙間
（後ろの窓が見える）、壁紙のぼかし、Files の Home の hero。

## 検証（2026-09-28、QEMU の Venus。実機は未実施）

- `demo-walk.sh`（最終、印の launcher を含む image `build/amd64/hdd-demo.img`、1920x1280）: 12 段とも撮れ、log の待ち（GREETER open、
  HANDOFF go、HOME opened、CORNER notes、LOCK locked reason=home・unlocked、SESSIOND SESSION end）は全部 ok。
  画面: `/home/awe/zedBSD-rpi4/build/ws035-shots/p116-20260929-demo-{00-splash,01-greeter,02-desktop,03-apphome,04-files,05-terminal,06-notes,07-pdfviewer,08-wiseview,09-lock,10-unlocked,11-logout}.png`。
- 回帰 PASS（1280x800）: zdesktop-p102（lock・unlock・idle。矢印と cursor の変更の入った demo の image、p117 の前）。p117 も入った lean な
  files の image で zdesktop-p053（wl_shm と cursor: client の cursor・隠す cursor・全画面）、zdesktop-p059・p062・p064（docking）、
  ws079 の zdesktop-p010（端のジェスチャー）。
- p059・p064 は最初 FAIL: compositor が socket を出すまで約 6.5 秒かかり（変更前の image `hdd-files.img` の compositor でも同じ 26×250 ms、
  3 回）、試験の固定の sleep（3・4 秒）では client が繋がらない。変更前の image でも p064 は同じく FAIL（既存）。両試験を socket ができるまで
  待つ形に直して PASS。
- build warning 0（wayland）。`plan/tools/style-check.py` greeter.c・input.c・compose.c・main.c・shell.c・glass.c 0。
- 未実施: 実機（5330 + HDMI の LCD）。zdesktop-p109（greeter の見た目の pixel 試験、graphical-network の image）は走らせていない
  （変えたのは Log In の文字だけで、画面は目で確認）。

## main への一覧（WS035 の範囲外、または判断が要るもの）

1. Notes の toolbar: 「Marker」と「Eraser」の間だけ間隔が広い（WS079）。
2. 窓の title の区切りが app で違う: PDF Viewer は「quilt.pdf - PDF Viewer」、Notes は「Notes — note-….pdf」（WS079）。
3. Terminal の `uname -a` が「zedBSD kei 0.0.1 zedBSD 0.0.1 x86_64」（kernel の sysname。WS078 の判断）。
4. compositor の wl_output の name「ZDESKTOP-1」と description「zdesktop output WxH」が client（browser など）に見える（WS078 の改名。
   `zdesktop-p078.sh` が期待値に持つ）。Vulkan の application name「zdesktop」は内部で UI には出ない。
5. 全画面の Notes の上に通常の窓（PDF Viewer）が来ると system bar が戻り、Notes の toolbar の上半分を覆う（GNOME と同じ振る舞い）。
   デモの Notes → PDF Viewer の流れで見える。全画面の窓が最前面でない間の bar の扱いは判断が要る（WS079 の D3 と合わせて）。
6. Files の Home の挨拶が account の名前（「Good morning, root」）。デモを root で見せるか、名前のある user を作るか。
7. App Home の icon は文字の monogram（Lock Screen と Log Out が同じ「L」）。icon は Future Work（2026-09-27 のユーザーの決定）。
8. compositor の起動（GPU を開いてから socket まで）が Venus で約 6.5 秒。固定の sleep の試験は他にも同じ理由で落ちうる。
9. PDF Viewer の Ctrl+O の chooser は素朴な一覧（WS079）。
