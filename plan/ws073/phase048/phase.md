<!-- awesome-plan project=zedbsd record=ws073-p048 -->
# ws073-p048: BUG-103 — /bin/sh の上下の矢印の履歴（PS/2 の keyboard だけの時）を確かめて閉じる

Status: in-progress（q651-i01、P1 generation12、2026-10-04。確認の試験を T1 に依頼、結果待ち）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-103](../../bugs/BUG-103.md)
Queue: q651（2026-10-04 user「P1、P2はBUG-143, 053, 052, 162, 103, 129, 033, 157, 139を修正します。直す順序は任せます。」）

## 範囲

BUG-103 の 2 つの原因（sh の履歴が file に残らない、PS/2 の keyboard の E0 の key が捨てられる）は WS087（p002・p005）で直り、
2026-10-03 の user の優先度は「低（直っていると思われる）。確認だけして閉じる」。作り直した repository の今の main で直っていることを確かめる。

## 読みでの確認（2026-10-04）

- `src/drivers/platform/pcat/ps2-8042.c` の `keyboard_build_capabilities()` は plain と E0 の両方の位置（`extended` 0・1）の key を capability に入れる（ws087-p005 の修正）。
- `src/kern/tty.c` の `tty_console_input_event()` は `INPUT_KEY_UP` を `ESC [ A` にして非 canonical の読み手（libedit）へ渡す。
- `userland/base/sh/history.c` は `HISTFILE`（既定 `$HOME/.sh_history`）を読み書きし、`stifle_history` で editor の履歴を `HISTSIZE` まで伸ばす（ws087-p002）。

## 検証

- host: `sh plan/tools/sh/build-host-sh.sh build/p1-q651/host-sh` と `python3 plan/tools/sh/history-host.py build/p1-q651/host-sh` → `history-host: 17/17`（2026-10-04）。
- QEMU（T1 に依頼、未実施）: [bug103-ps2-history.py](../tests/bug103-ps2-history.py) IMAGE OUTDIR。USB keyboard の無い q35（i8042 だけ）で
  console に root で login し、`echo ps2hist103`、Up、Return。画面の出力の行 `ps2hist103` が 2 つで PASS（修正前は 1 つ）。
- 実機（5330、内蔵の PS/2 keyboard）: 未実施。S2 の実機の確認に残す。

## 残り

- T1 の結果で Q1 が判定する。PASS なら QEMU の範囲で resolved を提案（実機の確認は S2）。
