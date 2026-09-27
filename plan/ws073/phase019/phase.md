<!-- awesome-plan project=zedbsd record=ws073p019 -->

# ws073-p019: setenv・putenv が既存の変数を置き換えると後ろの変数を全て落とす（BUG-079 の原因）

Status: cleared（2026-09-28）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-079](../../bugs/BUG-079.md)

## 原因

`userland/base/libc/posix.c` の `setenv()`・`putenv()` は、既にある変数を置き換えるときも新しい entry の直後に終端（`environ[slot + 1] = NULL`）を
書いていた。置き換えた変数より後ろの変数が全て見えなくなる。zdesktop の App Home の起動は子で `setenv("XDG_RUNTIME_DIR")`・
`setenv("WAYLAND_DISPLAY")` をしてから `execl()` する。SSH の shell の環境では XDG_RUNTIME_DIR が HOME・FOO より前にあり、置き換えで
それより後ろの変数が失われた（sh は PATH を補う）。rtld・libc の 2 度目の初期化・copy relocation は関係しない。

同じ関数の別の欠陥: 探索が終端を越えて `ENVIRONMENT_MAX` 個を読んだ。program が `environ` に自分の配列を代入した（POSIX が許す）後の
`setenv` は、その配列の外を読み書きした（修正前の probe は segmentation fault）。起動時の環境の複写は 64 個で黙って切れた。

## 修正（libc だけ）

- `environment_slot()`: 終端より前だけを探し、変数の entry の番号か、無ければ終端の番号（満ちていれば `ENVIRONMENT_MAX`）を返す。
- `environment_store()`: 置き換えは他の entry に触れない。新しい entry だけが終端を動かす。
- `environment_adopt()`: `environ` が program の配列なら、libc の配列へ entry を複写してから変える（program の配列と文字列は変えず、解放しない）。
  `setenv`・`putenv`・`unsetenv` が使う。`clearenv` は libc 自身の配列の時だけ自分の確保した文字列を解放する。
- `ENVIRONMENT_MAX` を 64 から 256 に。

## 検証（QEMU、amd64。実機は未実施）

- build: lean の disk-image（`-Werror`、warning 0）。規約: `tests/style-diff.py`（posix.c）0、新しい file（`tests/env-replace.c`）0。
- [tests/env-replace.c](../tests/env-replace.c)（guest の clang で compile、SSH）: 先頭の変数の setenv・putenv の置き換え、unsetenv、`environ` の代入、
  fork・setenv・execl。修正前の libc.so で segmentation fault（139）、修正後の libc.so（`LD_LIBRARY_PATH`）で `ENV:PASS`・`ENV:PASS child`、0。
- 起動の再現（lean の guest）: `env -i XDG_RUNTIME_DIR=/old PATH=/bin HOME=/tmp/dhome FOO=bar` から setenv 2 つと execl の `env`:
  修正前は XDG_RUNTIME_DIR・PATH・PWD・WAYLAND_DISPLAY だけ、修正後は HOME・FOO も残る。
- desktop（Venus の guest、main の `build/ws035-sq` の image の複製、image の libc.so は変えず `LD_LIBRARY_PATH=/tmp/newlib` で zdesktop を起動）:
  [tests/home-env.sh](../tests/home-env.sh)。Home の `Env` の項目の `env`: 修正前は HOME・FOO が無い（BUG-079 の観測どおり）、修正後は
  HOME=/tmp/dhome・FOO=bar・XDG_RUNTIME_DIR・WAYLAND_DISPLAY があり PASS。Home から起動した Terminal の `env | sort` の画面:
  `/home/awe/zedBSD-rpi4/build/ws073-shots/bug079-after/terminal.png`（FOO・HOME が見える）。
- boot test: `plan/tools/boot-test.sh build/ws073-env/hdd-image.img` PASS（login prompt）。
- 未実施: image に焼いた新しい libc.so での desktop の確認（main の次の desktop image で）。

## 残り

- ws035-p090 の回避（session の script が /root の folder を作る、p090 の試験の注記）は desktop の agent の判断で外せる。
