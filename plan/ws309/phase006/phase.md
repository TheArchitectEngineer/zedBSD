<!-- awesome-plan project=zedbsd record=ws309-p006 -->

# ws309-p006 daemon ×5 の thread（audiod・networkd・bluetoothd・phone・volumed）

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §4.8。Queue: q1662-i04（P1）。
- 状態: in-progress（2026-10-29 P1）。
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
- QEMU Venus: `KWL THREADS` に 5 本（の中で開いた物）、音量・network の bar が今どおり。

## 記録

- 2026-10-29 P1（3aa40a6c6）: 上の実装。amd64 `bin/wayland` exit 0・warning 0。bt-desktop-host-test PASS（実 thread、TSan は test の偽 daemon の中だけの race 2 件）、phone-backend-host-test PASS（inline）。
- 2026-10-29 P1: host-audio.c の `AUDIOD_VERSION`（main で既に build が壊れていた）を `AUDIOD_PROTOCOL_VERSION_1` に、fd の検査を今の形に直した（Q1 の依頼）。`host-audio: 14/14 passed`（TSan も）。
  `plan/ws309/tests/daemon-link/run.sh` 17/17 PASS（ASan+UBSan と TSan、stall の最長 21〜31 µs）。

## 再開点

- 2026-10-29 P1: 残り: arm64・riscv64 の build、QEMU（`KWL THREADS`、音量・network）、Linux/FreeBSD の build の確かめ（options に field を足した）。WS309 の完了の時は `daemon-link-inline.c` を ws197 の phone 試験のため plan/tools へ移す（Q1）。
