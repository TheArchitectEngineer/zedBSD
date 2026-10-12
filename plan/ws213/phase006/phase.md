<!-- awesome-plan project=zedbsd record=ws213-p006 -->

# ws213-p006: 規約の全文の見直し

Parent: [WS213](../ws.md)
Status: planned（最後）
Phase disposition: normal

## 範囲

- WS213 が変えた C（compositor と試験）の全部を plan/coding-style.md の全文と照らして書き方だけを直す（動作は変えない）。

設計は [p001](../phase001/phase.md)。

## 受け入れの条件

1. 変えた file の全部を plan/coding-style.md の 14 節の checklist で確かめた記録。
2. build warning 0（下のコマンド）、p002〜p005 の host 試験が全部 PASS、`git diff --check` 空。

## コマンドと上限

- build（warning 0）: `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/bin/wayland`、`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/bin/wayland`、`make -j16 keiland-linux`（各 900 秒まで）。FreeBSD は T1 の guest（`plan/tools/keiland-freebsd/backend-test.sh`）。
- host 試験は各 120 秒まで。同じ条件で変更なしの再試行は 3 回まで。QEMU・実機は T1 と Q1 経由（担当は QEMU を起動しない）。
