<!-- awesome-plan project=zedbsd record=ws113-p006 -->

# ws113-p006: Settings Displayページ

Parent: [WS113](../ws.md)
Status: in-progress（実装済み、T1 の QEMU 待ち）
Disposition: normal
Primary Milestone: MG006（WSから継承）
Queue / attempts: q855（P1、2026-10-07、ユーザー「Settingsのディスプレイ設定を、拡張・ミラーありで実装する作業に切り替えてください。」）
Purpose / goal: モード選択とドラッグ配置を提供
Prerequisites: p005 cleared/libkeiland公開API
Investigation bound: 90分の有限1 Phase Queue案。選定時にscope/時間を再照合する。

## Procedure / affected components

既存読み取り専用Displayページを実設定UIへ置換。出力識別、全拡張/全mirror、配置drag、適用/失敗/変更通知を実装。
[設計](../design.md)の対応段と[全文方針](../../standards/ws113-display.md)を契約とする。材料が変わればWSと影響する他Phaseへ同時反映し、既存Queueの実装scopeを拡張しない。

## Clearance / verification

Settingsからlibkeilandのみで切替と配置変更ができ、hotplug後の一覧を更新する。無効配置時は利用者に理由を表示。
結果は対象環境・source revision・commands・artifactsと結びつける。既存の実機/QEMUを混同しない。未解決の前提で調査上限に達したらattemptをunclearedとし、証拠と再開条件を記録する。

## Standards / limits / evidence

[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[scoped full rule](../../standards/ws113-display.md)、[automation](../../standards/automation.md#ws113-multi-display-coverage-2026-10-02)を実装前に読む。formatter/style-checkは補助、意味/所有/イベント順はfull/manual review。HAL API変更は差分ごとの事前承認。compositorのGPU UAPI直接ioctl禁止。無関係なtoolchain変更、aggregate make check、既存WS089/WS099のPhase改変は含めない。

Commands/results/commit/environment/artifacts/skipped checks: 未実施（計画のみ）。Findings: [現状調査](../design.md)。Resume: prerequisiteの実出力を確認し、このPhaseだけを新Queueへ選定・承認後に開始。

## p001契約調査による詳細化（2026-10-02）

[origin p001](../phase001/phase.md)、[契約](../phase001/contracts.md)、[ID/完了比較](../phase001/identity-completion.md)、[fixture](../phase001/fixtures.md)、[WS summary](../ws.md)を入力とする。

Procedure: public libkeilandだけでmode二択、出力一覧、extended配置drag draft/edge snap/Apply/cancel、mirror配置dragの無効化を実装する。Apply前にhardwareを変更しない。hotplug通知・stale再読込・backend failure・適用成功だが保存失敗を実状態に合わせて表示する。

Verification / resume: D02/D03/D08/D09を適用。driver/GPU/Vulkan direct call無しを監査。draftとauthoritative snapshotが競合する場合の利用者操作、接続0/1/2、未対応server、保存失敗を確認。WS089の旧stub履歴を遡及変更しない。

Status/dependenciesは上記のまま。未採択architecture/製品判断とactual prerequisiteを確認し、新QueueでこのPhaseだけを有限選定・承認後に実装する。q586はp001文書のみで後続sourceを許可しない。

## 採択済local port ID / native capability入力

[main採択A2とsource](../phase001/identity-completion.md)、[native capability結線](../phase001/native-contract.md)を使う。自Phaseへの影響: Settingsはkind/portから人向けlabelを表示、driver EDIDnameの同handle書換に依存しない。
後続の実装権限/依存は不変。actual API番号/layout/共有callback差分は選定前にowner/main review、HAL変更なら事前承認。

## Event

2026-10-02 / ws113-multidisplay-plan-20261002-ws113-p006-created: current userの5条件・3つの追加判断をこのPhaseへ投影。planned/Queue none。GitHub body/comment/Projectへの公開は保留。

2026-10-02 / ws113-contract-design-20261002-a3-ws113-p006: p001のsource/一次仕様で明らかになった不足に合わせ、上記の自Phase procedureと検証/resumeを詳細化。D02/D03/D08/D09を適用。driver/GPU/Vulkan direct call無しを監査。draftとauthoritative snapshotが競合する場合の利用者操作、接続0/1/2、未対応server、保存失敗を確認。WS089の旧stub履歴を遡及変更しない。 origin/WSリンクは上記。planned/Queue noneを保持。GitHub body/comment/Projectはmainへdelivery依頼pending。

2026-10-02 / ws113-technical-choice-20261002-a3-ws113-p006: mainのdelegated technical decision messageからD-BOOT/LAYOUT/REC/AUTH/PORT通常案を採択記録。自Phase影響: 非重複/辺連結edge snapを配置draft/Apply検査へ。 [origin](../phase001/phase.md)/[詳細](../phase001/identity-completion.md)/[WS](../ws.md)。依存/Queue権限不変、main remote delivery pending。

2026-10-02 / ws113-local-port-id-20261002-a3-ws113-p006: mainのD-ID A2/旧bootpreferred技術採択messageを受領。Settingsはkind/portから人向けlabelを表示、driver EDIDnameの同handle書換に依存しない。 [origin](../phase001/phase.md)/[sourceと範囲](../phase001/identity-completion.md)/[WS](../ws.md)。既往eventを保存し、該当current designを更新。p001 in-progress、他Phase planned/Queue none。main remote delivery pending。

## 2026-10-05 計画（q702、ベータ2）

入力: [契約の確定](../phase001/contracts-beta2.md) D-MODES・D-BRIGHT、contracts.md §8 の Settings の段。

範囲（`userland/desktop/settings/`、`page-look.c` の `se_display_draw` を新しい `page-display.c` へ）: libkeiland の `kl_system_displays_*` だけを使う。
- 頁の上: 出力の配置の図（出力ごとの card に label・解像度、内蔵は panel の印）。拡張の時は card を drag で動かせ、離すと辺に snap（重ならない・辺で連結）。drag は draft だけを変え、Apply で送る。
- モード: 「Extend」・「Mirror」の二択（segmented）。mirror の時は配置の drag を無効にする。
- 明るさ: snapshot に has_backlight の出力（内蔵の panel）がある時だけ slider（0〜100）。drag 中は 100 ms ごとに set_brightness、離した時に最後の値。Fn の key の変更は snapshot の変更で slider に反映。
- Apply・Revert、結果の表示（stale は再読み込みして「画面の構成が変わりました」、unsupported・backend_failed は理由、saved=0 は「保存できませんでした」）。hotplug で一覧を更新。出力が 1 つの時は配置の図を 1 つの card で示し、モードは選べるが効果は同じと示す。
- 解像度・refresh・回転・出力ごとの off は出さない（D-MODES）。

手順: 1) 頁の model（draft・snap の計算）を描画と分け、host の試験（`plan/ws089/tests/` の settings-render に Display の頁の絵、snap の計算の case）。2) 描画と入力（既存の Settings の widget）。3) 明るさの slider。

試験: host（settings-render の Display の頁の PNG: 1 出力・2 出力の拡張・mirror・明るさの slider あり／なし、snap の case）。QEMU（T1、Venus の 2 出力）: Settings の Display の頁で拡張 ⇔ mirror を 5 回、card の drag で左右を入れ替え（M2）、PNG。実機（p008）: 明るさの slider と Fn の key。
受け入れ: M2 の QEMU の PASS と PNG、host の PASS、warning 0、規約。目安 3h。依存: p005。衝突: WS089 の Settings の Phase と直列（`page-*.c`・`pages.c`）。

### D-LIMIT の反映（2026-10-05）

limited の出力の card を薄く描き「同時に表示できる数の制限で使えません」と示す（drag はできない）。hotplug や他の出力の解放で limited が外れたら通常の card に戻る。

## 2026-10-07 実装（q855、P1）

Q1 の ACK:「p006 の範囲で進めてよい。頁の文は他の Settings の頁と同じく英語を元にし、ja の翻訳（settings.tr）を足す。settings.h の drag の y は今 WS089 を触る担当がいないので可。」

| 区分 | 内容 |
| --- | --- |
| 配置の計算 | 新しい `userland/desktop/settings/arrange.c`・`arrange.h`: 平面を箱へ写す（`se_arrange_fit` は箱の 0.7 を埋めて中央、`_to_box`・`_from_box`）、drag した出力の snap（`se_arrange_snap`: 他の出力の 4 辺の隣、辺の長さを共有、重ならない、辺が 1/8 以内なら揃える、求めた位置に最も近い候補。出力が 1 つなら動かない）。描画・libkeiland を知らないので host で単体に試験する。 |
| 頁 | 新しい `page-display.c`（`page-look.c` の旧 `se_display_draw` を置き換え）。Displays の card: Extend・Mirror（選んだ方を primary）、配置の箱（limited でない出力の card、anchor は accent、拡張で 2 つ以上の時だけ card が drag の control）、出力ごとの行（大きさ・Hz、内蔵の印、limited は「Not shown: …」）、Apply・Revert（draft が違い、要求の待ちが無い時だけ）、結果の行。Brightness の card は BACKLIGHT の出力がある時だけ（slider、drag 中は 100 ms ごと・離した時に set_brightness）。system が無い・displays を出さない desktop では旧来の Mode・Graphics の読み取り専用の card。出力が 1 つの時は「With one display both show the same.」。 |
| draft | snapshot（`kl_system_displays_get`）と別の draft。KL_SYSTEM_CHANGED_DISPLAYS で snapshot を取り直し、編集中で同じ出力の集合なら draft を保つ、集合が変わったら draft を捨てて「The displays changed. Arrange them again.」。Mirror → Extend で配置が重なっていたら anchor から右へ一列に並べ直す（`display_spread`）。Apply は limited でない出力の place を送る（mirror は place 無し）。ESTALE は取り直して同じ文、EINVAL・EPERM・ENOTSUP・その他は各々の文。 |
| 結線 | `settings.h`（`struct se_display`、`se_app` の `drag_y` と `display`、宣言）、`ui.c`（drag の START・MOVE・END の前に `app->drag_y = event->y`）、`main.c`（`se_display_poll`）、`system.c`（`se_display_result` を結果の鎖へ）、`pages.c`（press・drag、要約「Arrange the displays and set the brightness.」、検索語に extend mirror arrange brightness）、Makefile・Makefile.linux・Makefile.freebsd に 2 file。 |
| 翻訳 | `userland/desktop/locale/settings.keys` の要約を差し替え、`ja/settings.tr` を `tools/i18n/tr.py update` で更新して 20 の文を訳した（check: 134 entries, 134 translated, 0 problems）。update が消した「Not asked for」の注記（Battery 等）は手で戻した。 |
| 試験 | `plan/ws113/tests/host-arrange.c`・`.sh`（fit・各辺・揃え・内側から・単独・3 出力・乱数 20000 回。snap の結果を compositor の `kwl_displays_validate` でも検査）。T1 用の `displays-p006.sh`・`config-amd64-p006.mk`（p005 の image に settings）。 |

確認（host、2026-10-07）:
- `sh plan/ws113/tests/host-arrange.sh` → `WS113 p006 arrange host test PASS (plain, ASan/UBSan)`
- `make -j16 BUILD=build/p1-wl ZEDBSD_CONFIG=plan/ws113/tests/config-amd64-p005.mk build/p1-wl/bin/settings` → exit 0、warning 0（-Werror）
- `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/p1-wl-linux all` → exit 0、warning 0
- `python3 plan/tools/style-check.py` page-display.c・arrange.c・arrange.h・host-arrange.c → 指摘 0
- FreeBSD の build は未実施（Makefile.freebsd に 2 file を足しただけ）。

未実施・制限:
- QEMU（T1 `displays-p006.sh`: Venus の 2 出力、拡張 ⇔ mirror を 5 回、card の drag で左右の入れ替え、PNG）。M2 の判定はこの結果。
- 実機（p008、5330）: 明るさの slider と Fn の key の追従。
- `plan/ws089/tests/host-build.sh`（settings-render）は p006 の前から link で落ちる（`kl_system_printers_*`・`kl_system_print_*`・`kl_system_power_get_state`・`preview_picture` の偽物が無い）。page-display.c・arrange.c の compile（gnu89、-Wall -Wextra -Werror）はそこで通ったが、Display の頁の host の絵は作れていない。settings-render を直すのは WS089 の範囲。
- 「保存できなかった（saved=0）」の表示: p005 の結果は errno だけで saved を運ばないので出していない。

## T1-367 の判定（2026-10-08 Q1）

FAIL（試験の道具）: displays-p006.sh の pointer() が `--width 1280 --height 800` を qmp-pointer.py に渡し `unknown step --width`、zdesktop-check.py の shot が D-Bus display で無い vnc.sock に繋ぐ（ConnectionRefusedError）。頁の表示・card が control・KWL FAILED 無し・生存は ok。
