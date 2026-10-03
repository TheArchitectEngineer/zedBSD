<!-- awesome-plan project=zedbsd record=ws137 -->

# WS137: FreeBSD の試験の VM を T1・T2 で使えるようにし、libkeiland-backend の FreeBSD の試験を流す

<!-- awesome-plan-current:start -->
Status: completed（2026-10-04）
Primary Milestone: MG006
Related Milestones: —
Objectives: O2
Parent: [Master](../master.md)
Queue: q658（P3）・T1-056
Resume point: なし。
<!-- awesome-plan-current:end -->

## 目標（2026-10-04 ユーザー）

「FreeBSDのVMイメージを用意して、T1,T2で使えるようにした上で、書くだけだったFreeBSDのテストも実行するようにお願いします。libkeiland-backendのビルドと実行のことです。」

## 結果

- 道具 `plan/tools/keiland-freebsd/`（README の WS137 の節）: `build-guest.sh`（公式の FreeBSD 15.1 の VM image を取得し、公式の CHECKSUM.SHA256 を q550 の記録と照合、NoCloud の seed、native の build の package）、`guest.sh`（start・stop・status・ssh・put・get・copy・shot、127.0.0.1 の転送ポートと QMP の PNG、serial の log は読まない）、`backend-test.sh`（guest の中で keiland-freebsd.mk を native に build、warning 0、install・audit・host 試験）。Master の Tools 節に登録。
- 確認: P3 の QEMU+KVM と T1-056（main fec887f）で、native の build（351 source、libkeiland-backend・libkeiland・compositor・app）warning 0、native-build-audit、host-seat-freebsd 13/13・host-session 31/31・host-power 17/17・6/6、sync-rejected・dmabuf-export-rejected が全て PASS。WS131 の p004〜p008 の phase.md に記録。

## 制限・移管

- guest に GPU が無い（i915 の passthrough は使わない規則）ので、FreeBSD の compositor の起動・表示・入力の確認は未実施。必要になったら別の WS で。
- 2 つの guest の同時の起動、`--force` の作り直し、他の checkout の image の共用は未確認（README に方法）。

## Phase

| Phase | 内容 | 状態 |
| --- | --- | --- |
| p001 | guest の道具と backend の build・試験の道具 | cleared（P3、988b9a0） |
| p002 | WS131 p004〜p008 の FreeBSD の build と試験 | cleared（T1-056 PASS 9/9） |
