<!-- awesome-plan project=zedbsd record=ws166-p004 -->

# ws166-p004: T1（QEMU）と全文の規約

Status: planned（2026-10-06 P2: T1 の部分は T1-196c で PASS 済み（p002・p003 の記録）。全文規約の見直しは、第 1 段の実装の 1 パスの後に WS177 とまとめて行う（2026-10-06 Q1）。実装の残りは無い）
Disposition: normal
Parent: [WS166](../ws.md)
依存: [p002](../phase002/phase.md)、[p003](../phase003/phase.md)

## 範囲

- T1: `plan/tools/guest/test-image.sh plan/ws166/tests/config-amd64-osk-predict.mk BUILD` の image で `plan/ws095/tests/ime-guest.sh start IMAGE` → `plan/ws166/tests/osk-predict-guest.sh OUTDIR`。合格: 最終行 `osk-predict: status 0`、PNG（predict-kan・chosen・space）。回帰: `plan/ws102/tests/osk-guest.sh` の send・emoji・history（同じ image に textedit が無ければ ime-probe の部分）。
- WS の全部の変更の全文の規約の確認（`plan/coding-style.md`）。
