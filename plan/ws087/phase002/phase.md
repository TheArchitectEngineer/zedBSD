<!-- awesome-plan project=zedbsd record=ws087-p002 -->

# ws087-p002: 矢印キーの履歴（BUG-103 の修正）

Status: cleared（2026-09-29、subagent の worktree `wt/ws087`。(a) の修正。(b) の原因は kernel の PS/2 driver で、修正は main に依頼）
Disposition: normal
Parent: [WS087](../ws.md)
Queue: main が割り当てた（2026-09-29、libedit の変更の許可つき: GNU Readline と同じ名前・型の追加だけ、既存の API と振る舞いを変えない、net・sh の build を確かめる）

## 目的と受け入れ条件

設計は [p001](../phase001/phase.md) の「p002」。ユーザーへの質問（(a) 新しい窓で上 / (b) 同じ窓でも上が効かない）の答えが来るまで (a) として進める。

1. 対話の shell（stdin が端末）が起動時に `HISTFILE`（unset なら `$HOME/.sh_history`、空なら file なし）を読み、fc と矢印の履歴に入れる。
2. 端末から読んだ空でない行を、読んだ直後に file の末尾へ 1 回の write で足す（O_APPEND、0600）。書けなくても shell は続ける。
3. 起動時に file が HISTSIZE の 2 倍の行を超えていたら、最後の HISTSIZE 行に縮める。
4. libedit に `stifle_history(int)` を足し、sh が起動時と HISTSIZE の変更時に呼ぶ（呼ばなければ 32 行のまま＝既存の振る舞い）。
5. 非対話の shell（script、`-c`、端末でない stdin）は読みも書きもしない。
6. 試験: host の pty（新しい shell で上が前の shell の命令を出す、HISTFILE、HISTSIZE の切り詰め、stifle で 32 行を超えて戻れる）、guest の ssh -tt、
   WS042 の差分試験（非対話が変わらない）と vi の host 試験、net と sh の build（warning 0）、boot test。

## 実装

- `userland/base/libedit/readline.c`・`readline/history.h`: GNU Readline と同じ `void stifle_history(int max)` を追加。履歴の配列を固定の 32 から
  可変（倍々に伸ばし、上限は `history_limit`）にした。呼ばなければ上限は `HISTORY_MAX`（32）のままで、既存の振る舞い（`net` の tool を含む）は変わらない。
  負の max は 0（GNU と同じ）、0 なら何も残さない。add_history は複写を先に作り、失敗したら何も落とさない（元の順と同じ）。
- `userland/base/sh/history.c`: `sh_history_load()`（`HISTFILE`、unset なら `$HOME/.sh_history`、空なら無し。最新 `HISTSIZE` 行を fc と矢印の両方の
  履歴に入れ、file が `2 * HISTSIZE` 行を超えていたら fc の履歴の内容で一時 file（`path.PID`）に書いて rename）、`sh_history_save()`（1 行を
  `O_APPEND|O_CREAT|O_CLOEXEC`、0600 で 1 回の write）、`sh_history_size_changed()`（HISTSIZE の hook。editor に stifle_history）。
- `main.c`: HISTSIZE の hook を登録。対話で stdin が端末のとき、profile と `$ENV` の後、`run_interactive()` の前に `sh_history_load()`。
- `input.c`: 端末から line editor で読んだ空でない行を `sh_history_save()`。端末でない入力（`sh -i` に pipe）は保存しない。
- `shell.h`: 3 つの宣言。

## 試験の結果

- host（`plan/ws087/tests/history-host.py build/ws087/host-sh`）: **17/17**。既定の file と 0600、新しい shell で上が前の shell の行を出す、2 つの shell の交互、
  `HISTFILE`、空の `HISTFILE`、`HISTSIZE=5` の読み込みと 2 倍を超えた file の切り詰め、2 倍未満は書き直さない、上 50 回で 32 行より前へ戻れる
  （stifle_history）、shell の中の `HISTSIZE=3` が editor を縛る、vi mode の `ESC 2k`、`-c`・pipe・pipe の `-i` は file を作らない。
- host の WS042 の差分試験（`plan/tools/sh/sh-diff.py`、oils は main の `build/ws042/oils` を読むだけ）: 変更前の host-sh（HEAD の git archive から build）
  1438/1458、変更後 1436/1458。差の 2 件は `oils/sh-options.test.sh` の `noclobber on &> >`・`&>> >>`（`&>` が `&` と `>` になり background の出力の順が
  揺れる）で、`--only sh-options` を 3 回ずつ流すと変更前も 27/29 が出る（揺れ）。sh の非対話の振る舞いの差ではない。
- host の vi（`plan/tools/sh/vi-host.py`、HOME は一時 directory）: **33/33**。
- build（amd64、`plan/tools/guest/build-ssh-image.sh build/amd64`、`-Werror`）: exit 0、warning 0。libedit・sh の全 file・`net/main.c` が build し直された。
- QEMU（amd64、新しい image、ssh -tt の login shell）: 1 つ目の session で `echo guest-one`・`echo guest-two`、2 つ目の session で上・上・上・Enter →
  `exit`・`echo guest-two`・`echo guest-one` と戻り `guest-one` を実行。`/root/.sh_history` は `-rw------- root`。
- boot test（`OUTPUT=build/ws087/boot-p002 plan/tools/boot-test.sh build/amd64/hdd-image.img`）: PASS（login prompt、`build/ws087/boot-p002/login.png`）。

## (b)「同じ窓で命令の直後でも上が効かない」の調査（2026-09-29、ユーザーの回答を受けて）

ユーザー:「上下キーは、同じ窓で命令を打った直後に上を押しても、何も出なかった、と思いますが、私の操作ミスかもしれません。」

実機との差: 5330 の内蔵 keyboard は PS/2（i8042、`src/drivers/platform/pcat/ps2-8042.c`、上は `E0 48` → `"up"`）。p001 の QEMU の試験は QMP の key が
USB keyboard（`usb-kbd`）に入っていた。そこで QEMU で USB keyboard を外し（QMP `device_del /machine/peripheral-anon/device[1]`）、PS/2 だけにして試した
（main の `build/ws035-sq/hdd-image.img`、Venus）:

| 経路 | 結果 |
| --- | --- |
| Terminal の中の `stty raw -echo; dd bs=1 of=/tmp/keys.bin`、QMP で a・上・下・左・右・Tab・b | `a \t b` だけ。**矢印は 1 byte も届かない** |
| compositor を止めて `dd if=/dev/input/event3`（PC/AT PS/2 keyboard）、a・上・下・左・右 | `EV_KEY 30`（a）の押下と離しだけ。**矢印の event は evdev にも出ない** |

**原因（kernel、QEMU で再現）**: `ps2-8042.c` の `keyboard_build_capabilities()` は capability の集合を E0 の付かない表（`scan_symbols[0..127]`）だけから
作る。`drv_input_device_emit()`（`src/drivers/generic/input.c`）は capability に無い code の event を queue にも subscriber（console）にも出さない
（`drv_input_capability_event()` が key_bits を見る）。そのため E0 の key（上下左右・Home・End・PageUp/Down・Insert・Delete・右 Ctrl/Alt・Meta）は
`scan_symbol()` で code に変換されても捨てられる。USB keyboard（QEMU の既定、p001）では起きない。

修正案（main へ依頼。`src/drivers/` は WS087 の範囲外で、HAL の API ではない）: `keyboard_build_capabilities()` で `extended` の 0 と 1 の両方について
`scan_symbol(scan, extended)` を引いて capability に足す（`keyboard_capabilities[256]` に収まる）。確かめ: PS/2 だけの QEMU で上の 2 つの試験に矢印が出ること、
Terminal と console で上が履歴を出すこと。

## 実機（5330）での確認手順（main が ssh で行う。未実施）

1. evdev: `dmesg | grep 'input:'` で `PC/AT PS/2 keyboard` の node（QEMU では event3、5330 は event1 の見込み）を確かめ、`plan/ws084/tests/evlat.c`
   （guest 向けに build したもの）で `evlat /dev/input/eventN 10` を走らせ、内蔵 keyboard の a と上を押す。期待: 修正前は a（code 30）だけ、
   修正後は上（`EV_KEY` code 103）も出る。
2. Terminal: 実機の Terminal で `stty raw -echo; dd bs=1 count=8 2>/dev/null | od -c; stty sane` を打ち、上・下・左・右・a・b・c・d を押す。
   期待: 修正前は `a b c d` だけ（8 byte に満たず待つので、足りなければ文字を追加で押す）、修正後は `033 [ A 033 [ B` …。
3. 修正後、Terminal で `echo one` の後に上 → `echo one` が出ること、新しい Terminal で上 → 前の窓の命令が出ること（p002）。

## 未実施

- 実機（5330）での確認（上の手順、main が実施）。PS/2 の kernel の修正（main）。
- aarch64 の build（試験は amd64 だけの指示）。

## Resume point

cleared。次は p003（Tab の補完）。
