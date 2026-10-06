<!-- awesome-plan project=zedbsd record=ws170-p003 -->

# ws170-p003: Phone の app（連絡先の一覧とタイムラインの表示）

Status: cleared（2026-10-07 Q1 の判定: T1-298 の AAT（needs-person）を Q1 が PNG で目視: Phone の SEND・STATUS・RECEIVED の log と Echo の PNG、backend 0 で拒否）（旧: test-wait（T1-294））
Disposition: normal
Parent: [WS170](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p002](../phase002/phase.md)・[p004](../phase004/phase.md)

## 実装（2026-10-07、P2）

設計は [p001](../phase001/phase.md) §2。

- `phone.h`: 試験の data の型を store の型に（item に日時・state・file の path、連絡先に ID と item の配列）。view の request の queue（`ph_view_take_request`）。
- `view.c`: 未読の bitmap をやめ、連絡先を開くと既読の request。Send・Call は window への request、添付は「Attachments cannot be sent yet.」。一覧の題の右に「+」、新しい連絡先の form（Name・Number、Save・Cancel）を timeline の場所に。連絡先が無ければ「No contacts yet」。
- `main.c`: 起動で `~/Documents/Phone` を開く。compositor の phone（`KL_SYSTEM_HAS_PHONE`）を使う: 送信は「sending」で保存して `kl_system_phone_send`（channel は連絡先の最後の message の物、無ければ RCS）、通話は通話の item を保存して `kl_system_phone_call`、状態の事象と request の結果（ENODEV は「No phone backend」の notice）で item の state を書き直す。受信は番号の連絡先（無ければ作る）に未読で保存し `kl_app_notify`、開いている連絡先なら既読。log は長さと番号の無い行だけ。
- p000 の試験の data（`data.c`）は program から外して `plan/ws170/tests/host-phone-data.c` へ（試験の folder の store に入れる関数）。
- 試験の image の config `plan/ws170/tests/config-amd64-phone.mk` に `keiland-settings` を足した（AAT が phone.backend を設定する）。

## 確かめ

- host: `sh plan/ws170/tests/run-host-phone.sh` → PASS 17（p000 の 15 の send・call を request の log に、「+」の form と Save の request を足した。絵 `build/ws170/host-phone-start.png`・`host-phone-glass-add.png` を目で見た）。
- build: `make -j16 ZEDBSD_CONFIG=plan/ws170/tests/config-amd64-phone.mk BUILD=build/ws170-zed build/ws170-zed/bin/phone` warning 0。style-check（phone の全 file）0。
- AAT: `tests/scenarios/apps/phone/browse.md` を store に合わせて書き直し、`message.md` を新規（p004 と一緒）。helper は `plan/tools/aat/scenarios/helpers_phone.py`（`helpers_apps.py` の古い browse を外した）。`check-scenarios.py` PASS。
- 未実施: QEMU（T1）。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS170 p003 の行。
