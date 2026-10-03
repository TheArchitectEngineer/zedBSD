<!-- awesome-plan project=zedbsd record=ws084p003 -->

# ws084-p003: L2: 素の 5330 で demo の既定の image が 10 回中 10 回 greeter（自動の login の desktop）まで届く

Phase ID: `ws084-p003`
Parent: [WS084](../ws.md)
Status: planned（2026-10-01 作成。ws.md の「段の計画」の L2 と「進め方とハーネス」から）
Phase disposition: normal
承認: なし（Queue に入れるときに main が承認を記録する）
依存: p002（cleared）。ユーザーが 5330 を USB から起動できること。

## 目的

ws.md の L2: 素の 5330 で、demo の image（最新、既定の logo と `kmsg=quiet`、`display=edp`）の起動が 10 回中 10 回、黒い画面や固まりなく
greeter（demo の image は kei が自動で login するので、その desktop）まで届く。firmware の画面の takeover（p001・p002）を毎回通る。

今まで素の 5330 で確かめた takeover は `ZEDBSD_GRAPHICAL_BOOT=n` の image（demo-lcd2・demo-lcd3）だけ。**既定の graphical boot（loader の
splash が GOP の画面に出た後に takeover）の image は素の 5330 で未確認**（master の Outlook の demo-lcd8 も logo 無効）。この Phase で初めて確かめる。

## 範囲

- 新しい script `plan/ws084/tests/reboot-loop.sh`（この WS の試験。source の変更ではない）。
- demo の既定の image の build と QEMU の boot test、素の 5330 での 10 回の起動、ユーザーの目視（最初と最後）。
- driver の修正は範囲の外。失敗したら uncleared で終え、原因の見立て（dmesg の行）を書き、p004 か新しい Phase に回す。

## 完了の条件

1. 素の 5330 で 10 回の起動の全てで、ssh が戻り、dmesg に `i915: N0 decision: PROCEED`・`i915: takeover: rc=0`・
   `i915: resident display: picture up` があり、`pipe_off wait timed out`・`LCD-B preflight: the display is not idle`・`resident display: not started` が無く、
   `ps -A` に compositor（`wayland`）が居る。
2. ユーザーが 1 回目と 10 回目の LCD を目で確かめる（黒・固まり・崩れ 0、写真があれば `build/ws084-p003/`）。
3. QEMU の boot test が同じ image の複写で PASS（QEMU には firmware の画面が無く takeover を通らないので、これは起動の回帰だけ）。
4. 証拠を素の 5330 と QEMU に分けて書く。

## 手順

repo の root から。手引きは [guide.md](../guide.md)。

1. image（他の image の build と重ねない）と boot test:
   ```
   mkdir -p build/ws084-p003
   plan/ws075/demo/build-demo-image.sh build/ws084-p003-usb > build/ws084-p003/build.log 2>&1; echo "exit=$?"
   grep -E ':[0-9]+:[0-9]+: warning:' build/ws084-p003/build.log | grep -vE '/packages/|^\.\./src/|userland/base/noct/noct/' | wc -l
   cp build/ws084-p003-usb/hdd-image.img build/ws084-p003/boot-copy.img
   OUTPUT=build/ws084-p003/boot plan/tools/boot-test.sh build/ws084-p003/boot-copy.img; echo "exit=$?"
   ```
   **`login=graphical` を足さない**（graphical boot の既定と重複して起動が止まる）。boot test の PNG（`build/ws084-p003/boot/login.png`）をユーザーに見せる。
2. `plan/ws084/tests/reboot-loop.sh` を書く（仕様は下）。`bash -n plan/ws084/tests/reboot-loop.sh` で構文を確かめる。
3. ユーザーに依頼する: `build/ws084-p003-usb/hdd-image.img` を USB に書き、5330 の firmware の起動順で USB を先にし、USB から起動、
   LCD に desktop が出たら IP（前回は 10.0.30.5）を知らせてもらう。**この間は passthrough（solaris10-man）の試験はできない**（同じ機械）。
4. 1 回目の確かめ（reboot の前）:
   ```
   ssh -i plan/tmp/guest/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@<IP> dmesg > build/ws084-p003/boot0.dmesg
   grep -E 'N0 decision|takeover:|resident display|pipe_off|LCD-B preflight' build/ws084-p003/boot0.dmesg
   ```
5. 10 回の reboot:
   ```
   plan/ws084/tests/reboot-loop.sh <IP> 10 build/ws084-p003/loop | tee build/ws084-p003/loop.txt
   ```
   1 回目の reboot で ssh が 300 秒戻らなければ、firmware が USB 以外（NVMe の Linux）から起動したか reboot が効いていない。止めてユーザーに画面を
   聞く。その場合の代わり: ユーザーが電源で 10 回起動し、毎回 4 の command を走らせる。
6. 最後の起動の LCD をユーザーが目で確かめる。
7. 結果を下の「結果」に（各回の ssh が戻るまでの秒、判定、失敗した回の dmesg の該当行）。

## reboot-loop.sh の仕様

```
plan/ws084/tests/reboot-loop.sh HOST COUNT OUTDIR
```

- ssh の引数: `-i plan/tmp/guest/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 root@HOST`
  （image の root の authorized_keys は `build-demo-image.sh:42` がこの鍵の .pub を入れる）。
- 各回 n（1..COUNT）: `ssh ... '/sbin/reboot'`（接続が切れて非 0 で返ってよい）→ ssh が失敗するまで待つ（最大 60 秒）→ ssh が戻るまで 5 秒ごとに試す
  （最大 300 秒、戻るまでの秒を記録）→ 30 秒待つ（session と compositor の起動）→ `dmesg > OUTDIR/boot$n.dmesg`、`ps -A > OUTDIR/boot$n.ps`。
- 判定（各回 1 行 `REBOOT-LOOP n ok|fail seconds=S reason=...`）: 完了の条件 1 の行の有無。最後に `reboot-loop: OK/COUNT ok`。
- 1 回でも ssh が 300 秒戻らなければ、そこで止める（`fail reason=no-ssh`）。
- QEMU の console・serial は使わない（素の 5330 の guest に ssh で尋ねるだけ）。

## 未知

- zedBSD の `/sbin/reboot`（`userland/base/reboot`、`shutdown-control/main.c`）が素の 5330 を再起動できるか（ACPI の reset）。1 回目で分かる。
- firmware が reboot の後も USB から起動するか（ユーザーの機械の設定、ws.md の注意）。
- dmesg の buffer（`kern.msgbuf`）が 30 秒後も起動の行を保つか。`perf:` の行で押し出されるなら、待ちを短くするか grep を先にする。

## 結果

（未実施）
