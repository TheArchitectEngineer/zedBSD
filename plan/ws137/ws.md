<!-- awesome-plan project=zedbsd record=ws137 -->

# WS137: FreeBSD の試験の VM を T1・T2 で使えるようにし、libkeiland-backend の FreeBSD の試験を流す

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: —
Objectives: O2
Parent: [Master](../master.md)
Queue: q658（P3）
Resume point: p001。
<!-- awesome-plan-current:end -->

## 目標（2026-10-04 ユーザー）

「FreeBSDのVMイメージを用意して、T1,T2で使えるようにした上で、書くだけだったFreeBSDのテストも実行するようにお願いします。libkeiland-backendのビルドと実行のことです。」

## 完了の条件

1. FreeBSD 15.1 の QEMU+KVM の guest を作る道具 `plan/tools/keiland-freebsd/build-guest.sh`（公式の VM image `FreeBSD-15.1-RELEASE-amd64-BASIC-CLOUDINIT-ufs.qcow2.xz` を取得し CHECKSUM.SHA256 で確かめ、cloud-init の seed で SSH の鍵、native の build に要る package を入れる）と、起動・停止・SSH の道具（`plan/tools/keiland-freebsd/guest.sh`、Linux の `plan/tools/keiland-linux/guest.sh` と同じ形）。guest の image は worktree の `build/keiland-freebsd/guest` に作り、T1・T2 が自分の build/ に作れる（共有の build/ の物を読み取り専用でも使える）。手順は README に。
2. guest の中で libkeiland-backend（と WS131 で FreeBSD を「書くだけ」にした compositor・libkeiland の部分）を native に build（warning 0）し、host 試験（host-seat-freebsd ほか）と、guest の中で動かせる試験を流す道具（例 `plan/tools/keiland-freebsd/backend-test.sh`）。
3. WS131 の p004〜p008 の「FreeBSD は書くだけ」の項目を T1・T2 で流し、結果を各 phase.md に書く（直しは担当の WS の Phase）。
4. 規則: AGENTS.md「検証」の WS109 の FreeBSD guest の例外（127.0.0.1 の転送ポートの SSH と QMP の screendump の PNG、serial・console の log で判定しない）。i915 の passthrough は使わない（virtio の GPU か GPU 無し）。

## Phase

| Phase | 内容 | 状態 | 依存 |
| --- | --- | --- | --- |
| p001 | guest の道具（build-guest.sh・guest.sh・README）と backend の build・試験の道具、T1・T2 への手順 | planned（q658、P3） | — |
| p002 | WS131 p004〜p008 の FreeBSD の build と試験を T1・T2 で流し、結果を記録 | planned | p001 |
