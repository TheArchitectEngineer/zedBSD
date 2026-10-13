<!-- awesome-plan project=zedbsd record=ws218 -->

# WS218: Intel WiFi（AX211）driver の license の表示を実態に合わせる（OpenBSD・Intel の license の削除の監査）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-13 Q1 作成、P1 に p001 を割り当て）
Primary Milestone: MG005
Related Milestones: なし
<!-- awesome-plan-current:end -->

## 原文（2026-10-13 ユーザー）

「WSの追加
・Intel WiFiドライバがOpenBSDのドライバと類似していないことを確認して、OpenBSDのライセンスを削除する。引用元としての表示は、引用箇所に残す。
・Intel WiFiドライバの定数がIntelのヘッダの名前を使っていないことを確認し、テーブルも仕様書から導けることをを確認して、Intelのライセンスを削除する。引用元としての表示は、引用箇所に残す。」

## 目的と完了の条件

対象は `src/drivers/wifi/intel-ax211/` と、その license の表示（`tools/release/license-components.json` の `intel-ax211-driver`、`/usr/share/licenses/intel-ax211-driver/LICENSE`、source の頭の注記）。

1. OpenBSD の iwx(4) の source と比べ、構造・関数の分け方・表現が類似していない（独立に書かれた）ことを、比較の方法と結果つきで記録する。類似が見つかった所は書き直すか、license を残す判断をユーザーに返す。
2. 定数の名前が Intel の header（iwlwifi の `iwl-*.h`・`fw/api/*.h` など）の名前を使っていないこと、table（command の id・TLV・register の値など）が公開の仕様書（または firmware の公開の format の記述）から導けることを、出典つきで記録する。導けない値は出典の候補をユーザーに返す。
3. 1・2 が確かめられたら、OpenBSD・Intel の license の本文と inventory の記載を外し、引用元としての表示（「この値は … による」など）は引用の箇所に残す。firmware の package（`intelax211-firmware`）の license は対象外で変えない。
4. license の inventory の試験（`plan/ws129/tests/license-inventory-test.sh`）が PASS、build の warning 0。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| p001 | 監査: OpenBSD iwx(4) との比較、定数の名前と table の出典の調べ、結果の記録（削除の可否の判断材料） | planned |
| p002 | 削除と引用の表示の整理（p001 で確かめられた範囲だけ） | planned |

## ユーザーの決定（2026-10-13、p001 の後）

- 「Intel WiFiのOpenBSD由来については、引用を明示して、著作権を外します。それは著作権法上、認められているのではないでしょうか。外していいと思います。」→ OpenBSD の license を外し、引用元の表示を引用の箇所に残す（p001 の (D)）。MSI-X の cause の表（transport.c:130-134）は p001 の推奨どおり書き直す。
- 「net80211 の作者（Onoe・Leffler）の注記を外して、出典を IEEE 802.11 に書き換えてよい。」→ bss.c・bss.h の注記を外し、出典を IEEE 802.11 にする。
- Intel の部分（2026-10-13 ユーザー、原文）:「intel_ax211_context_info_gen3は、構造体のコメントに出典を明記すれば、Intelの著作権表示なしでOKです。」「Intelのヘッダも、シンボル名を変えて、関連する連続するマクロ定義の先頭や、enumのコメントなど、ブロックごとに出典を書いて、シンボル名が一致しないようにして、Intelの著作権表示を外します。ファイルヘッダに、Intelのヘッダを1次資料として、値を収集した旨、書いておいてください。翻案であるとの指摘を受けないように、これまでの実装からわかっている事実をコメントでブロックごとに書き加え、定数がどこでどのようい利用され、どのような意味を持つかなど、詳細に記述してください。他のOSのドライバ開発者にとって有用なドキュメントになる体裁であれば、翻案ではなく独自の価値をもつ資料になり得ます。」→ p002 で: 約 170 の定数と struct の tag・Intel に特有の field 名を付け直し（Intel・iwx の名前と一致しない）、連続する macro・enum の block ごとに出典（Intel の header の file と版）と、zedBSD の実装から分かった事実（どこでどう使い、何を意味するか）の comment を書き、各 file の頭に「Intel の header を 1 次資料として値を収集した」旨を書き、Intel の著作権表示と license の file・inventory を外す。`intel_ax211_context_info_gen3` は名前を付け直し、構造体の comment に出典を明記する。
- 2026-10-13 ユーザー（原文）:「ほかの OS のドライバを作る人にも役立つ資料であるから、これはIntelヘッダの翻案ではないと、ファイルヘッダに書きましょう。以上で問題ないです。」→ 各 file の頭に、値を集め実装から分かった使い方と意味を独自に書き加えた資料で Intel の header の翻案ではない旨を書く。p002 の範囲はこれで確定。
