<!-- awesome-plan project=zedbsd record=ws228-p004 -->
# ws228-p004: kernel の VT console の CSI に引数 2 つの `H` と最小限の `m` を足す

Status: in-progress — 実装済み・Q1 が merge（2026-10-14）、T1-596 の試験待ち（2026-10-14 P1、q973。依頼の番号は Q1 が付ける）
Disposition: normal
Parent: [WS228](../ws.md)
Queue: q973
依存: 無し（vi の p001 の画面が console で正しく出る前提。vi の側に回避を入れない）

## 由来

[review-1](../../../build/review/ws228-p001-review-1.md) の F1: `src/kern/tty.c` の `tty_render`（1528-1569）は CSI の中の `;` を見るとその列を丸ごと捨て、`tty_console_csi`（1415-1493）は引数を 1 つしか持たず `H` は常に (0,0)、`m` を扱わない。terminfo の `zed`（`userland/base/terminfo/zed.zti`）は `cup=\E[%i%p1%d;%p2%dH` を申告しているので、vi・less などの全画面の命令が console で壊れる。
ユーザーの決定（2026-10-14 クリック）:「kernel の console を直す」。

## 範囲

- CSI の引数を複数（少なくとも 2 つ、省略は既定値）持てるようにし、`H`・`f`（行;桁、1 起点）を実装する。`m`（SGR）は 0（解除）・1（太字）・7（反転）・27・22 と前景・背景の 30〜37・39・40〜47・49 の最小限（表示できない属性は無視して列は捨てない）。
- 既存の 1 引数の列（A・B・C・D・G・J・K など）の振る舞いを変えない。知らない終端・private の列（`?`）は今まで通り捨てる。
- `zed.zti` の申告と console の実装を突き合わせ、申告と実装が食い違う capability を一覧にして phase.md に書く（直すのは cup と SGR に関わる物。他は一覧だけ）。
- HAL の API は変えない（tty の内部だけ）。`plan/coding-style.md` の全文を適用。

## 確かめ

- amd64 の build warning 0。tty の CSI の解析を host で試せる形があればその試験（無ければ解析の関数を切り出して host 試験を作る）。
- QEMU（T1）: console の login の後に `printf '\033[5;10HX\033[7mR\033[0m'` などで位置と反転を PNG で判定、vi（p001 が merge 済みなら）を console で開いた画面の PNG。依頼の文を phase.md に。

## 結果（q973、P1、2026-10-14）

### 実装

- 新しい `src/kern/tty-csi.c`・`src/kern/tty-csi.h`: kernel の状態にも表示にも触れない CSI の読み手（host の試験と共有するため切り出した）。
  - 引数を最大 16 個（`;` 区切り、空は「書かれていない」）。値は 1000 を超えたら伸ばさない（以前と同じ上限）。17 個目からは読んで捨てる。
  - private の印・`:`・中間の byte（0x20〜0x3f の数字と `;` 以外）を含む列は終端まで読んで捨てる（以前と同じ）。0x20〜0x3f の外の byte は終端（制御文字も以前どおり終端）。
  - ESC の後が `[` でなければ ESC とその byte を文字として出す（以前と同じ）。
  - `tty_csi_position`: `H`・`f` の行;桁（1 起点、空と 0 は 1、画面の外は端で止める）。
  - SGR: 0・空（解除）、1・22（太字＝前景の明るい色）、7・27（反転）、30〜37・39、40〜47・49。他の番号は 1 つずつ飛ばし、`38`/`48` の `;5;n`・`;2;r;g;b` は引数ごと飛ばす（数字を別の属性と読まない）。属性の byte は VGA 形式（ANSI の色の番号を VGA の bit の順に直す。背景の bit 3 は VGA の text で点滅になるので使わない）。
- `src/kern/tty.c`:
  - VT ごとの `console_escape_*` の 4 つの配列を `struct tty_csi_parser console_escape[]` にし、`console_attribute[]`（今の SGR）と `console_attribute_marked[]`（画面に通常でない属性の cell があるか）を足した。`tty_console_init` で初期化、`tty_vt_activate` で履歴の再生の前に戻す。
  - `tty_console_csi`: `H`・`f`（2 引数）と `m` を足した。`A`・`B`・`C`・`D`・`G`・`J`（2 だけ）・`K` の振る舞いは変えていない（第 1 引数だけを使い、書かれていなければ 1）。`ESC[2J` で marked を落とす。
  - `tty_console_text`: 通常の属性で marked でなければ以前どおり `kern_text_putc` の列。それ以外は 1 byte ずつ `kern_text_putc`（折り返し・scroll・serial の mirror はそのまま）の後、その cell を `kern_text_write` で属性付きで書き直す（最終行の最後の桁で scroll した時は 1 行上の cell）。pcat の putc は cell の古い属性を残し、pc98 の putc は通常の属性で書くので、どちらでも「putc の後に書き直す」で揃う。rpi4 は teletype で write も cursor も効かない（変化なし）。
- 6 つの `platform/*/vmunix.mk` に `tty-csi` を 1 語ずつ足した（amd64・arm64・x68k・sparcv9 は source、pcat・pc98 は object の列）。
- HAL の API（`include/hal/hal.h`）と `kern_text_ops` は変えていない。

### zed.zti の申告と console の実装の突き合わせ

| capability | 申告 | console の実装（この Phase の後） | 扱い |
| --- | --- | --- | --- |
| `cup` | `\E[%i%p1%d;%p2%dH` | 実装した | 直した |
| `bold`・`rev`・`sgr0`・`setaf`・`setab`・`init`（`\E[0m`）・`colors=8`・`pairs=64` | SGR | 実装した | 直した |
| `bce` | 有り | 消去（`K`・`J`）は常に通常の属性の空白で、今の背景の色を使わない | **zed.zti から外した**（curses が背景の色の付いた空白を自分で書く） |
| `clear`（`\E[H\E[2J`）・`el`・`cuu1`・`cuf1`・`cuf`・`cr`・`cub1`（pcat） | | 合う | — |
| `ed`（`\E[J`） | 画面の終わりまで消す | `J` は 2 だけを扱い、`\E[J`・`\E[0J` は何もしない | 一覧だけ（**食い違い**、vi が使うなら要る） |
| `reset`（`\Ec`） | RIS | `ESC c` は ESC と `c` を文字として出す | 一覧だけ（食い違い） |
| `civis`・`cnorm`（`\E[?25l/h`） | cursor の表示 | private の列として捨てる（cursor は常に表示） | 一覧だけ（害は無い） |
| `tbc`（`\E[3g`）・`hts`（`\EH`） | tab stop の設定 | `g` は捨てる、`ESC H` は文字として出る。tab は 8 桁固定 | 一覧だけ（食い違い） |
| `bel`（`\a`） | 音 | pcat の putc は 0x07 を glyph として描く | 一覧だけ（食い違い） |
| `cols=80`・`lines=25` | 既定の大きさ | 実際は framebuffer の大きさ（winsize が正しい値を持つ） | 一覧だけ（winsize が優先される） |
| `cub1`（pc98） | `\b` | pc98 の putc の `\b` は cell を消す | 一覧だけ（pc98 の driver） |
| rpi4 の console | — | teletype（serial）で、CSI は tty が読んで捨て、位置も属性も出ない | 一覧だけ（rpi4 は cup が効かない） |

制限: 属性は ASCII の表示できる byte（0x20〜0x7e）にだけ付く。UTF-8 の多 byte の文字は 1 byte ずつは書き直せないので通常の属性で出る。pcat の scroll は新しい行を cursor の行の先頭の cell の属性で埋める（driver の既存の振る舞い）。

### 確かめ（host・build）

| 確かめ | command | 結果 |
| --- | --- | --- |
| amd64 の kernel | `make -j8 build/amd64/vmunix`（worktree の config.mk、`-Wall -Wextra -Werror`） | rc 0、warning 0（`tty.o`・`tty-csi.o` を build） |
| host 試験 | `cc -std=c99 -O2 -Wall -Wextra -Werror -Isrc src/kern/tty-csi.c tests/tty-csi-host-test.c -o build/tmp/tty-csi-host-test && build/tmp/tty-csi-host-test` | `tty-csi-host-test: PASS`（位置 9・1 引数の列 5・捨てる列 4・SGR 14・上限 2 の群） |
| C89 | `cc -std=c89 -pedantic -Wall -Wextra -Werror -Isrc -c src/kern/tty-csi.c` | rc 0 |
| 試験の image の config | `make ZEDBSD_CONFIG=plan/ws228/tests/config-amd64-console.mk BUILD=build/ws228-p004 -n build/ws228-p004/vmunix` | 読める（dry run だけ） |

未実施: pcat・pc98・arm64・x68k・sparcv9 の kernel の build（amd64 だけ）。QEMU（T1 に依頼）。vi の画面（p001・p002 が未 merge）。実機。

### T1 への依頼（Q1 経由）

- image: P1 の commit を merge した main の SHA で `make -j16 ZEDBSD_CONFIG=plan/ws228/tests/config-amd64-console.mk BUILD=<dir> disk-image`（CI の amd64 に text の起動と VT の console の login）。
- 手順: QEMU（amd64、既定の display）で起動し、VT の console で root で login。次を打つ（QMP の send-key で文字として。`\033` は 4 文字の文字列のまま打つ）:
  1. `clear; printf '\033[5;10HX\033[7mREV\033[0m\033[1mB\033[0m\033[31;44mC\033[0m\033[12;40HY\033[20;1H'`
  2. `printf 'abc\033[2Dz\n'; printf 'p\033[?25lq\033[?25h\n'; printf 'n\033[0mo\n'`
  3. 画面を QMP の screendump で PNG に撮る。
- 合格（PNG で判定）:
  - 1 行目は空（clear の後）、5 行目の 10 桁目に `X`、続く 11〜13 桁に反転（明るい灰の地に黒）の `REV`、14 桁に明るい白の `B`、15 桁に青の地に赤の `C`。12 行目の 40 桁目に `Y`。prompt は 20 行目以降に出る。
  - 2 の出力が `azc`・`pq`・`no`（1 引数の `D` が以前どおり、private の列が文字として出ない、`ESC[0m` が文字を食わない）。`REV` の後の文字や prompt が反転のまま残っていない。
  - 画面の他の場所に `[`・`;`・`H`・`m` などの列の欠片が出ていない。
- vi: ws228-p001/p002 が merge 済みなら、同じ console で `vi /etc/rc` を開いた画面の PNG（本文が 1 行目から並び、最下行に状態の行）。未 merge なら省略し、p002 の確かめに回す。
- 結果の返し先: Q1（PASS/FAIL、PNG の path、image の tree の SHA）。

### 残り

- T1 の試験の結果（Q1 が判定）。
- `ed`（`\E[J`）は vi が使うなら別の Phase で実装する（この Phase の範囲は cup と SGR）。p001 の設計が `ed` を使うかを P3 に確かめてほしい。
- `tests/tty-csi-host-test.c` を回帰に使うなら、master.md の試験の一覧に登録する（Q1 の判断）。
