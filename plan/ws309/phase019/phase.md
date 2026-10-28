<!-- awesome-plan project=zedbsd record=ws309-p019 -->

# ws309-p019 試験の screen capture（keiland-shot）の画素の送りを thread へ

- 親: [WS309](../ws.md)。Queue: q1662-i01（P1）。
- 状態: in-progress（2026-10-29 P1）。
- 出典: 2026-10-29 Q1「P2 が見つけた compositor の不具合: keiland-shot で撮った後、compositor がその window へ frame callback を送らなくなり、client の libvulkan の FIFO の
  10 s の watchdog が切れて SURFACE_LOST（errno 24 = EAGAIN）。…shot の経路の frame callback の扱いを、今の thread の整理の中で直して（独立の小さな修正にできるなら先に commit して SHA を）。」
  P2 の診断: [ws291 phase006](../../ws291/phase006/phase.md) の「window mode の shot の SURFACE_LOST（errno 24）の解析」（RDNA の実機、main の kernel でも、撮らなければ 900 frame 完走）。

## 見立てと直し

- `shot.c` の `kwl_shot_complete`（frame の完了、`compose_hold` の中）は、画素（1080p で約 8 MB、4K で約 33 MB）を event loop の thread で blocking の `write`（`SO_SNDTIMEO` 10 s）で送っていた。
  その frame の frame callback は直前に client の出力へ積まれるが、flush は pass の後なので、読み手が遅い間は main が止まり、client は frame callback を受けない。止まりが 10 s に届くと
  FIFO の client の libvulkan の watchdog（`WAYLAND_WAIT_NS` = 10 s）が切れる。仮説（コードの読み）。RDNA の実機で同じ原因かは未確認。
- 直し（WS309 の原則 1「main は待たない」）: 画素を buffer から写して（malloc と memcpy、main で数 ms）、専用の thread（`shot-send`、threads.c の台帳）が header と画素を書く。
  main は次の request を、送りが終わって join するまで受けない（stage `SHOT_SENDING`）。thread が立たない時は今の同期の書き込み。終了の時は送りを待つ。

## 受け入れ

- QEMU Venus（`plan/ws309/tests/shot-fifo.sh`、image `plan/ws309/tests/config-amd64-p1.mk`）: mview（FIFO、900 frame）の 6 s で keiland-shot、mview が status 0（DONE）と shot=0。
  `SHOT_STALL=15`（keiland-shot を 0.3 s の後 15 s 止めて遅い読み手にする）で、直した版は mview が status 0、直す前の版は止まる（仮説の確かめ）。
- `bin/wayland`（試験の config）warning 0。
- RDNA の実機（P2 の round の shot の段）は Q1 の割当で（未実施）。

## 記録

- 2026-10-29 02:10〜 P1: shot-fifo.sh（stall 無し）: 直す前の binary（build/ws309/bins/wayland-old）も mview status 0・900 frame DONE・shot=0（QEMU の 1280x800・4 MB では読み手が速く、再現しない）。
- 同 02:14〜02:16 `SHOT_STALL=15 sh plan/ws309/tests/shot-fifo.sh build/ws309/shot-stall build/ws309/img/hdd-image.img build/ws309/bins/wayland-shot1 build/ws309/bins/wayland-old`:
  直した版・直す前の版とも mview status 0・900 frame DONE、`KWL SHOT sent bytes=4096000`。keiland-shot を 15 s 止めても直す前の版の write は止まらなかった
  （4 MB は unix socket の送りの buffer に収まったとみられる、未確認）。→ QEMU では仮説を確かめられず、P2 の RDNA の不具合が直るかは未確認。
- 判断: 直し自体は原則 1（main は待たない）に沿う改善として残す（commit）。RDNA の shot の段での確かめ（P2 の round、4K なら約 33 MB で buffer を超える）を Q1 に頼む。
  再現しない時の次の手がかり: compositor の `--log-frames` で shot の前後の `KWL LAT`・`KWL PERF pass` を見て、main が止まったか、frame を出し続けて callback だけ落ちたかを分ける。
- build: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/amd64 build/amd64/bin/wayland`（shot-none.c、main.c の KWL THREADS）と
  `ZEDBSD_CONFIG=plan/ws309/tests/config-amd64-p1.mk BUILD=build/ws309/img build/ws309/img/bin/wayland`（shot.c）が exit 0・warning 0。
