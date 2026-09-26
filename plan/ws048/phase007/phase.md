<!-- awesome-plan project=zedbsd record=ws048p007 -->

# ws048-p007: 規約の全文の確認と回帰

Phase ID: `ws048-p007`
Parent: [WS048](../ws.md)
Status: planned
Queue: —
HAL の承認: 不要
依存: p002〜p006

## 範囲

- WS048 で変えた全ての source を `plan/coding-style.md` の全文で見直す（`style-check.py` と手で）。
- 回帰: rpi4 と amd64 の `make -j16`（warning 0）、QEMU raspi4b と amd64 の `boot-test.sh`、WS048 の host 試験。
- 実機の確認の結果（p002〜p006）を取りまとめる。

## 受け入れ条件

1. 新しい file の `style-check.py` が 0 件、既存の file は WS048 の前より増えない。手の確認の記録。
2. build・boot test・host 試験が通る。
3. 実機の結果が各 Phase に書かれている（未実施は未実施と）。
