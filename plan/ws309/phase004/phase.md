<!-- awesome-plan project=zedbsd record=ws309-p004 -->

# ws309-p004 io-device の残り: 電源・network の詳細・sysmon（machine と detach の ack の扱い）

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §2 T6・§4.5。Queue: q1662-i03（P1）。
- 状態: cleared（2026-10-29 03:28 P1。実機は未実施。detach の ack は p016 へ、見積もりは Q1 に送った）。
- 出典: ユーザーの決定 3「I/O系はまとめていいです。」と ws.md の構成の表（I/O-1 device: 入力の device の検出、電源の読み取り、`statfs`、network・電源・アカウントの処理）。
- 依存: p003。

## 設計（実装した形）

- io.c: io-device の lane を全ての backend で起動する（`io_started`）。入力の device の find と close を lane で行うのは、backend が自分で device を開く時だけ（`io_input`、今は zedBSD）。
  他の部品の job は `kwl_io_device_submit`（thread が無ければ ENOTSUP、呼ぶ側が main で行う）。
- system.c: 電源の読み（`kl_backend_power_get_state`）と network の details（links・DNS・保存した network）の専用の pthread（`struct system_job`、読みごとに `pthread_create`）をやめ、
  読みごとに確保した job を io-device に出す。1 つずつ（`power_reading`・`details_reading`）、details の最後の読みは `details_read` に写す。鍵の保存（io-file、p005）と details は同時に走らない順のまま。
- backend-host.c: bar の電源（`server->power`）の読みを、power の変化（`kwl_backend_power_changed`）では io-device の job（`kwl_power_read_later`、読み中の変化はもう 1 度）に。
  起動（READY の前）と sleep の答え（その場の lid の状態を規則に渡す）は今どおり同期の `kwl_power_read`。読めなかった時は actions も 0 にする。
- power-dialog.c: Power Off・Restart の可否を、開く時に backend に聞かず、最後に読んだ `server->power.actions` で決める（起動と各変化で読む）。
- sysmon.c: 監視の常駐の thread（`sysmon_run`、cond の timedwait、join）をやめ、main が周期ごとに 1 つの標本の job を io-device に出す（前の job が取られてから）。info は 5 標本ごと。
  backend の monitor は io-device の thread が持つ（最初の job で open、最後の object の後の close の job で close、終了は thread の後で main が close）。標本と info は done で main の写しになり、serial で object に知らせる。

## 判断の点（Q1 の判断、2026-10-29 03:2x）

- machine の読み（machine-shell.c: 名前・file system の大きさ（statvfs）・account・mount）: 設計は T6 だが、今のコードは同時に 1 本の detached の thread で、10 s で諦めて query を failed で答え、
  止まった読みが残る間の query は即 failed、終了でも待たない。T6 に載せると止まった mount の後ろで入力の device の検出と終了の join が止まる（design のレビュー 10 の懸念と同じ）。
  今の形は main を待たせない（lock の短い区間だけ）ので、この Phase では変えない（その都度の thread、同時に 1 本）。
  Q1「machine の読みを T6 に載せず、今の『その都度の thread、同時に 1 本』で残す案は OK（Q1 の判断）。ただしユーザーの方針（thread は名前と本数を明示、アドホックにしない）に合わせ、
  名前を付けて threads.c の台帳（KWL THREADS）に載せ、design.md の thread の表に『machine-read、その都度 1 本、10 s で諦める』と書いて。」→ threads.c に `kwl_thread_start_detached`・`kwl_thread_ended`
  （signal を block して detached で作り、台帳に名前を載せ、thread が終わる時に自分で外す）を足し、machine-shell.c は `machine-read` で作る。design.md の表に A2 の行と T6 の注記を足した。
  Q1「sleep の後の lid の読みと起動の読みを同期のまま残すのも OK、表の注記に。」→ T6 の行に注記。
- input の detach の ack の同期の待ち（`kwl_input_thread_detach_wait`、最大 100 ms）: 通るのは seat の callback（input_gone・input_paused・input_resumed・session_paused）だけで、zedBSD の backend はこれらを呼ばない
  （zedBSD の device の終わりは input thread の detached の event → p003 の io-device の close）。Linux・FreeBSD の seat は callback の後で fd を閉じる・revoke する契約なので、ack を wake で受ける形は
  seat の open と一緒に p016（J4）で扱う。

## 受け入れ

- `build/amd64/bin/wayland` warning 0（arm64・riscv64 も区切りで）。
- QEMU Venus: `plan/ws309/tests/io-device2-guest.sh`（KWL THREADS に io-device、dump に power、details の result が error=0 で interface あり、monitor の 2 回の run の各々に info と 5 frame 以上、
  compositor が subscribers を 1・0・1・0 と数える）。
- 実機は未定（Q1 の割当）。

## 記録

- 2026-10-29 03:1x P1（027de114f・48869e96d）: 上の実装。`make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/amd64 build/amd64/bin/wayland` exit 0・warning 0。
- 2026-10-29 03:2x P1: QEMU Venus `sh plan/ws309/tests/io-device2-guest.sh build/ws309/io-dev2a <image> build/ws309/bins/wayland-p004a build/ws309/bins/libkeiland-p004a.so build/ws309/bins/keiland-system` → `io-device2-guest: PASS`
  （image は前の worktree `p1-ws309` の build/ws309/img/hdd-image.img。`KWL THREADS count=2 names=io-file,io-device`、dump の `KEILAND-SYSTEM power source=0 percent=-1`、details の result error=0、
  monitor の 2 回の run で frame は計 22、`KWL SYSTEM monitor subscribers=` が 1・0・1・0）。QEMU に電池は無く、power の変化の event（`kwl_power_read_later`）の経路は通っていない。
- 同 03:2x P1: machine-read の台帳（上の判断）。amd64・arm64（config-rpi4）・riscv64（config-visionfive2-keiland）の bin/wayland exit 0・warning 0、host の lane・io-file の試験 PASS。machine の読みの QEMU は未実施（形は今と同じ、thread の作り方だけ変えた）。
- 未実施: 実機、Linux・FreeBSD の build と QEMU（io-device が Linux・FreeBSD でも起動するようになった）。
