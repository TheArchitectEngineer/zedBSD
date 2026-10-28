<!-- awesome-plan project=zedbsd record=ws309-p006 -->

# ws309-p006 daemon ×5 の thread（audiod・networkd・bluetoothd・phone・volumed）

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §4.8。Queue: q1662-i04（P1）。
- 状態: cleared（2026-10-29 P1。実機・Linux/FreeBSD の build は未実施）。
- 出典: ユーザーの決定 5「daemonと接続するスレッドは、daemon接続ごとに単体に分離しましょう。」、J6（Q1 の決定: main で blocking の I/O をする接続だけの 5 本）。
- 依存: p002。

## 設計（実装した形）

- 新しい `libkeiland-backend-zedbsd/daemon-link-zedbsd.[ch]`: daemon の接続ごとに 1 本の thread。thread が connect・送受信・networkd の timed read・phone の MMS の file I/O を持つ。
  main の API（`kl_backend_<daemon>_update/get_*/request/take_*`）の形は不変: update は link の lock の下で thread が publish した写しを取るだけ、依頼は op（malloc した要求）を渡して thread の pipe に 1 byte。
  thread は op を順に走らせ、step（旧 update の中身）、publish（lock の下で写す。新しい物があれば共通の wake pipe に 1 byte）、poll（接続・自分の pipe・期限、最大 250 ms）。
- 名前: `audiod`・`networkd`・`bluetoothd`・`phone`・`volumed`（`KWL THREADS` に載る）。
- backend の口: `kl_backend_options.threads`（`struct kl_backend_threads`: wake の fd、`start`（compositor の `kwl_thread_start_lent`）、`ended`（`kwl_thread_ended`））。backend.c が写しを持ち `kl_backend_threads()`（backend-private.h）で渡す。link は weak で参照（daemon だけの host 試験は backend.c 無しで link する）。
- compositor: wake pipe を `kwl_wake_open` で `kl_backend_open` の前に作る（`kwl_threads_prepare` は box だけ）。終了は `kwl_system_close`（bluetooth・phone）→ `kwl_network_close`・`kwl_media_close`・`kwl_volume_close` → `kl_backend_close` → `kwl_wake_close`。
- 意味の変化（Q1 OK）: 送れなかった依頼の errno は結果（DONE・result・REACHABLE）で返る。引数の誤りと最後の写しで分かる ENOTCONN・EBUSY は同期のまま。phone の EBUSY（pages 4・requests 32・MMS 1）は「thread の数（publish）＋渡して未実行の op の数（serial）」で同期に判定。
- networkd の request・scan の接続は poll に入れない（zedBSD の poll は自分の SHUT_WR を POLLERR で即返す）。答えを待つ間は 10 ms ごとに見る。

## 受け入れ

- amd64・arm64・riscv64 の `bin/wayland` warning 0。
- host: `plan/ws309/tests/daemon-link/run.sh`（ASan+UBSan と TSan: lent の maker と ended、wake、volumed の一覧・mount、accept しない volumed の間の main の呼び出し < 5 ms、networkd の EBUSY・DONE・送れない依頼の ENOENT が結果で）、
  `plan/ws143/tests/bt-desktop-host-test.sh`、`plan/ws197/tests/phone-backend-host-test.sh`（link は `plan/ws309/tests/daemon-link-inline.c`）、`plan/ws100/tests/host-audio.sh`。
- QEMU Venus: `plan/ws309/tests/daemon-guest.sh`（最初の tick の後の `KWL THREADS` に 5 本、audiod・networkd の reachable、scan と volume の result が error=0、2 回目の dump に頼んだ音量、SIGTERM から 10 s 以内に終わる）。
- compositor: `KWL THREADS` を READY と、bar の daemon を開く最初の tick の後（shell.c、`threads_told`）の 2 回出す。

## 記録

- 2026-10-29 P1（3aa40a6c6）: 上の実装。amd64 `bin/wayland` exit 0・warning 0。bt-desktop-host-test PASS（実 thread、TSan は test の偽 daemon の中だけの race 2 件）、phone-backend-host-test PASS（inline）。
- 2026-10-29 P1: host-audio.c の `AUDIOD_VERSION`（main で既に build が壊れていた）を `AUDIOD_PROTOCOL_VERSION_1` に、fd の検査を今の形に直した（Q1 の依頼）。`host-audio: 14/14 passed`（TSan も）。
  `plan/ws309/tests/daemon-link/run.sh` 17/17 PASS（ASan+UBSan と TSan、stall の最長 21〜31 µs）。

- 2026-10-29 P1: QEMU Venus `sh plan/ws309/tests/daemon-guest.sh build/ws309/daemon2 build/ws309/img/hdd-image.img build/ws309/bins/wayland-p006a build/ws309/bins/libkeiland-p006a.so build/ws309/bins/keiland-system` → `daemon-guest: PASS`
  （image は前の worktree `p1-ws309` の build/ws309/img/hdd-image.img の写し。`KWL THREADS count=7 names=io-file,io-device,bluetoothd,phone,volumed,networkd,audiod`、audio left=40 の後の dump、`stopped seconds=5`。
  終了の 5 s は p005 の binary（wayland-p005b）でも同じ 5 s で、今回の変更の物ではない）。1 回目（daemon1）は試験の誤り（`--timeout=90000` が usage で拒まれ、`kill -0` が zombie を生きていると数えた）で FAIL、直して 2 回目。
  amd64・arm64（config-rpi4）・riscv64（config-visionfive2-keiland）の bin/wayland exit 0・warning 0（shell.c の変更の後に作り直し）。
- 未実施: 実機、Linux・FreeBSD の build（`kl_backend_options` に field を足した。Linux・FreeBSD の backend は daemon-link を使わない）、bluetoothd・volumed が居る guest での動き（この image の volumed の一覧は 0 件、bluetoothd は controller 無し）。

## 再開点

- 2026-10-29 P1: p006 は cleared。WS309 の完了の時は `daemon-link-inline.c` を ws197 の phone 試験のため plan/tools へ移す（Q1）。
