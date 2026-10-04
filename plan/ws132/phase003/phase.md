<!-- awesome-plan project=zedbsd record=ws132-p003 -->
# ws132-p003: Keiland の backend の事象と compositor の input の探し直し・電池の表示

Status: planned（2026-10-05 Q1）
Disposition: normal
Parent: [WS132](../ws.md)
Focused goal: fg019（ベータ1）
Queue: q717 / q717-i01（P1）

## 範囲

[p001](../phase001/phase.md) の設計の U1 と compositor の分のうち、人間の判断 D1・D2 に依らない物:

1. libkeiland-backend の事象の口（events-zedbsd.c）: `/dev/system` を購読し、compositor の main loop に fd を渡す。Linux・FreeBSD の backend は stub（ベータ1 までは不要、2026-10-05 ユーザー）。OS の境界の checker（plan/tools/keiland-os-boundary/check.sh）を通す。
2. compositor: INPUT の add・remove で input の device を探し直す（USB キーボード・マウスの後挿し）。
3. compositor: AC・BATTERY の事象と `KERN_SYSTEM_GET_POWER` で system bar に電池・AC の状態を出す（電池が無い機械では出さない）。
4. POWER・LID の事象は受け取って記録するだけにし、動作は [p008](../ws.md)（D1・D2 の後）。

## 受け入れ

build の warning 0（zedBSD と Linux の keiland-linux.mk）、host の試験、OS の境界の checker が PASS。QEMU は T1 に依頼（USB キーボードの後挿しで入力が効く、電池の無い guest で bar に電池が出ない）。

## 所有 path

`userland/desktop/` の libkeiland-backend・compositor の該当の file、`plan/ws132/`。
