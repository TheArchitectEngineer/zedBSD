<!-- awesome-plan project=zedbsd record=ws309-p007 -->

# ws309-p007 font の thread: 予備の font・emoji の先読み、rasterize、color emoji の decode

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §2 T8・§4.6。Queue: q1662-i04（P1）。
- 状態: cleared（2026-10-29 P1。実機は未実施）。
- 出典: ユーザーの決定 4「blur、フォントの先読み、字形のラスタライズは、UXにクリティカルなので、単体の常駐スレッドにしましょう。」
- 依存: p002。

## 設計（実装した形）

- 今の glyph worker（glyph-worker.[ch]）を T8 にした: thread は `kwl_thread_start` で台帳に `font` の名で載る。job に `task` を足し、RENDER（今の rasterize）・READ（font の file を
  `KWL_GLYPH_FILE_MAX` まで読む）・COLOR（emoji の font の自分の face で `kl_color_glyph`、PNG の decode と縮小）の 3 つ。
- glass.c: worker の起動の時に予備の font（`server->fallback_font_path`）と emoji の font の READ を出す（先読み）。届いた bytes は main が `glass_add_face` で自分の face を開く（I/O 無し）。
  届くまで、開いた font に無い文字は NULL（幅 0、描かない。layer の絵は guard の evictions に数えて描き直す）。worker が無い時は今どおり最初の文字で同期に読む。
- color emoji: main は新しい `kl_color_glyph_metrics`（picture/color-glyph.c、PNG の header だけで箱・位置・advance）で cell を決め pending にし、COLOR を出す。結果は walk の始めに atlas へ写す
  （`glass_take_color`、世代と箱を確かめる）。job が作れない時は今どおり main で decode。
- metrics（`glass_text_width` など）は今どおり main の face で同期に引く（設計 §4.6、face を T8 だけに持たせない）。
- 範囲の外に残した物: 時計の大きな数字（`large_fill`）の `kwl_compose_quiesce` と `vkDeviceWaitIdle` は p009（遅らせた破棄）と一緒に。rasterize は既に worker。

## 受け入れ

- amd64・arm64・riscv64 の `bin/wayland`、amd64 の libkeiland・textedit（color-glyph.c を含む）warning 0。
- QEMU Venus: `plan/ws309/tests/font-guest.sh`（`KWL THREADS` に font、予備の font と emoji の font が thread で開く、日本語の title の U+65E5 が face=1、U+1F600 が `color=1 pending=1` で thread に出て decode の失敗が無い）。

## 記録

- 2026-10-29 P1（1ec63b366）: 上の実装。amd64 bin/wayland exit 0・warning 0。
- 2026-10-29 P1: QEMU Venus `sh plan/ws309/tests/font-guest.sh build/ws309/font2 build/ws309/img/hdd-image.img build/ws309/bins/wayland-p007a build/ws309/bins/libkeiland-p006a.so build/ws309/bins/titlebar-probe` → `font-guest: PASS`
  （`KWL THREADS count=3 names=io-file,io-device,font`、READY の後に `fallback font: ... faces=2`・`emoji font: ... errno=0`、`U+65E5 ... face=1 cell=0 pending=1`、`U+1F600 ... face=emoji cell=8 color=1 pending=1`）。
  画面: `build/ws309/font2/title.png`（worktree の中。title の「日本語のタイトル 😀 試験」が漢字・仮名・emoji とも描けている）。1 回目（font1）は image の古い titlebar-probe が `show FAIL errno=6` で、tree から作った probe を入れて 2 回目。
  arm64（config-rpi4）・riscv64（config-visionfive2-keiland）exit 0・warning 0。
- 未実施: 実機、font の file が無い時・壊れた時の QEMU（`no fallback font` の経路は今と同じ行）。
