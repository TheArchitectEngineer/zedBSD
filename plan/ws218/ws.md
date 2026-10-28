<!-- awesome-plan project=zedbsd record=ws218 -->

# WS218: Intel WiFi（AX211）driver の license の表示を実態に合わせる（OpenBSD・Intel の license の削除の監査）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。p001 の監査は済み、p002 は実装・build・host 試験・機械比較済み、残りは AX211 の QEMU・実機の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG005
Related Milestones: なし
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

`src/drivers/wifi/intel-ax211/` が OpenBSD の iwx(4) と類似していないこと、定数の名前が Intel の header の名前を使わず table が公開の資料から導けることを確かめ、OpenBSD・Intel の license の表示（`tools/release/license-components.json` の `intel-ax211-driver`、`/usr/share/licenses/intel-ax211-driver/LICENSE`、source の頭の注記）を外し、引用元の表示を引用の箇所に残す。firmware の package（`intelax211-firmware`）の license は対象外。

## ユーザーの決定

- 2026-10-13（原文）「WSの追加
・Intel WiFiドライバがOpenBSDのドライバと類似していないことを確認して、OpenBSDのライセンスを削除する。引用元としての表示は、引用箇所に残す。
・Intel WiFiドライバの定数がIntelのヘッダの名前を使っていないことを確認し、テーブルも仕様書から導けることをを確認して、Intelのライセンスを削除する。引用元としての表示は、引用箇所に残す。」
- 2026-10-13（p001 の後）「Intel WiFiのOpenBSD由来については、引用を明示して、著作権を外します。それは著作権法上、認められているのではないでしょうか。外していいと思います。」
- 2026-10-13「net80211 の作者（Onoe・Leffler）の注記を外して、出典を IEEE 802.11 に書き換えてよい。」
- 2026-10-13（Intel の部分）「intel_ax211_context_info_gen3は、構造体のコメントに出典を明記すれば、Intelの著作権表示なしでOKです。」「Intelのヘッダも、シンボル名を変えて、関連する連続するマクロ定義の先頭や、enumのコメントなど、ブロックごとに出典を書いて、シンボル名が一致しないようにして、Intelの著作権表示を外します。ファイルヘッダに、Intelのヘッダを1次資料として、値を収集した旨、書いておいてください。翻案であるとの指摘を受けないように、これまでの実装からわかっている事実をコメントでブロックごとに書き加え、定数がどこでどのようい利用され、どのような意味を持つかなど、詳細に記述してください。他のOSのドライバ開発者にとって有用なドキュメントになる体裁であれば、翻案ではなく独自の価値をもつ資料になり得ます。」
- 2026-10-13「ほかの OS のドライバを作る人にも役立つ資料であるから、これはIntelヘッダの翻案ではないと、ファイルヘッダに書きましょう。以上で問題ないです。」

## 結果

- p001: OpenBSD iwx(4) との比較と、定数の名前・table の出典の監査（P1、機械比較の道具は scratchpad）。MSI-X の cause の表（transport.c）は書き直す判断。
- p002: 約 170 の定数と struct の tag・Intel に特有の field 名の付け直し、block ごとの出典と実装から分かった事実の comment、各 file の頭の注記（1 次資料・翻案ではない旨）、bss.c・bss.h の出典を IEEE 802.11 に、Intel の著作権表示と license の file・inventory の除去。build・host 試験・機械比較まで（P3）。
- 削除を頼んだ 3 path（`userland/base/licenses/intel-ax211-driver/` の LICENSE・Makefile、`plan/ws004/tests/run-intel-ax211-driver-license-test.sh`）は tree に無い（2026-10-28 に確かめた）。
- AX211 の QEMU・実機の回帰は未実施。

## 制限・移管

- 実機の確認は未実施（AX211 の接続の回帰）。AX211 の出典の整理は WS248 と同じ扱い。
- p002 の記録に残る 2 つの事実（latency tolerance の no-snoop、SOC の flag）を直すかは判断されないまま閉じた。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws218-p001 | 監査: OpenBSD iwx(4) との比較、定数の名前と table の出典 | in-progress のまま閉じた（監査は済み、ユーザーの決定で p002 へ） |
| ws218-p002 | license の表示の削除、引用の表示、名前の付け直し、block ごとの出典の comment | in-progress のまま閉じた（実装・build・host 試験・機械比較済み、QEMU・実機は未実施） |

## 記録の所在

Phase の計画・結果は削除する。git の commit `fb1f6afee` の `plan/ws218/` にある。Queue の履歴は [plan/history](../history/index.md)。
