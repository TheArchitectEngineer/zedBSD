<!-- awesome-plan project=zedbsd record=ws071p002 -->

# ws071-p002: 骨格（window・present・canvas・text・icons、静的な配置、host の render 試験、guest の image）

Phase ID: `ws071-p002`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main の Queue への反映は main の session）

## 範囲

[design.md](../design.md) §2〜§4 の骨格: `userland/base/zdesktop-files`（main・window・present・canvas・text・icons・ui・ui-grid・
dir・mime・places）、build（package の Makefile、`platform/amd64/vmunix.mk` の link の規則）、host の試験の道具（`host-build.sh`、
`host-render.c`、`host-run.sh`、`make-home.sh`）、guest の image と試験（`config-amd64-files.mk`、`build-files-image.sh`、
`files-guest.sh`、`files-p002.sh`）。操作は sidebar・パンくず・Back/Forward/Home・folder の double click と wheel まで
（選択・keyboard・list 表示は p003）。

## 受け入れ

1. zdesktop-files が warning 0 で build でき、新しい file 全部が `style-check.py` 0。
2. host の render 試験で、toolbar・sidebar・content（icon の格子）が描け、sidebar の click と folder の double click で場所が
   変わる（画面を見る）。
3. Venus（QEMU）の zdesktop --glass の上で窓が出て、sidebar の click・double click・Back・Forward が動く（log と画面）。

## 結果（2026-09-27）

cleared。

- 実装: `userland/base/zdesktop-files/`（canvas.c 1100 行余り: 角丸・影・gradient・多角形・線・mask・画像の拡縮、text.c: libtruetype と
  fallback の face と glyph の cache・UTF-8・省略・折り返し・大文字の中心の baseline、icons.c: 線の icon 23 種と folder・書類、
  ui.c: 配置・toolbar（Back/Forward/Home・パンくず・検索欄・表示切替・preview）・sidebar・hit の記録と pointer・履歴・folder の変化の
  検出、ui-grid.c: icon の格子、dir.c: 一覧・自然順の並べ替え・大きさと項目数の文、mime.c: 拡張子の表と先頭 byte の署名、places.c、
  window.c: xdg toplevel・pointer・keyboard・key の repeat・fm_event の ring、present.c: CPU の canvas を linear の image に写して
  1 枚の四角で swapchain に貼る、main.c、shaders/ と shaders.h）。`platform/amd64/vmunix.mk` に link の規則と basic command の
  除外を足した。
- build: `make ZEDBSD_CONFIG=plan/ws071/tests/config-amd64-files.mk BUILD=build/amd64 build/amd64/bin/zdesktop-files` が warning 0
  （-Werror）。`python3 plan/tools/style-check.py userland/base/zdesktop-files/*.c` 0。host の build（gcc -Werror）も warning 0。
- host の試験: `sh plan/ws071/tests/host-build.sh` と `sh plan/ws071/tests/host-run.sh ...`（HOME は build/ws071-host/home）。
  build/ws071-host/a.png（Home: toolbar・sidebar・folder の格子）、b.png（Documents: 書類の icon と拡張子、日本語の名前、2 行の名前）を
  見た。途中で直した不具合: glyph の top の符号（字が上下にずれた）、descent の符号（行の中心）、2 行の名前が大きさの行に重なる。
- **QEMU（Venus）**: `sh plan/ws071/tests/build-files-image.sh`（build/amd64/hdd-image.img、zdesktop-files・fallback font・make-home.sh
  入り）、`sh plan/ws071/tests/files-guest.sh start`、`sh plan/ws071/tests/files-p002.sh` **PASS**（READY 1000x640、sidebar の
  Documents、Back、double click で Documents、Back・Forward、zdesktop の ERROR 0）。画面 build/ws071-p002/home.png（zdesktop の
  浮いたタイトルバー「Files」の下に toolbar・sidebar・Home の 7 folder）、folder.png（Documents、日本語の file 名が fallback font で
  出る）を見た。
- 実機（i915）: 未実施（p011 で任意）。
- 残り・気づき: sidebar は窓が低いと下が切れる（scroll は p005 の sidebar の編集と一緒に）。Home は p006 まで $HOME の格子で代える。
