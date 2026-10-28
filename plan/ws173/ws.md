<!-- awesome-plan project=zedbsd record=ws173 -->

# WS173: AAT（Agent Acceptance Test）— エージェントが素の実機を SSH で操作して受け入れを確かめる枠組み

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」。根拠: p001〜p004・p006 cleared（/dev/input-inject・keiland-shot・plan/tools/aat/・シナリオの runner）。残りは p005（素の 5330 での AAT）の実機の確認だけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

/dev/input-inject のマウスとキーボード、試験の image だけの画面の撮影、SSH の host の道具で、エージェントが素の実機（5330）を操作して受け入れを確かめる（AAT）。UAT はデバイス系と使用感に絞り AAT の後に遅らせる。

## ユーザーの決定

- 2026-10-05 夕（Q1 の問い「実機をベアメタル起動したときに、SSH越しにマウスイベントを投げて、スクリーンショットを撮り、それを取得するようなコマンドを作れますか？」への案に）「では、それを実装して、AAT (Agent Acceptance Test)としてください。UATの必須確認項目は今挙げてくれたようなデバイス系と、全体の使用感に絞ります。AATの枠組みが完成した時点で実装されている機能でイメージを作成して、AATを実施、フィードバックを記録してください。UATはそのあとに遅らせます。UATの時刻は追ってお知らせします。」
- 承認: `/dev/input-inject` にマウス（相対の移動・button・wheel）とキーボードを足す UAPI（`include/uapi/input-inject.h`）。画面の撮影の口は試験の image だけ（製品には入れない）。UAT の必須の確認はデバイス系（電源 button・蓋・USB の抜き差し・touchpad の指・YubiKey と NFC の実物・音・Wi-Fi の実機の電波・S0 idle）と全体の使用感。
- 2026-10-05「AATはテストシナリオのドキュメントを元にエージェントが操作できるように設計してください。特殊な設計というわけではないですが。シナリオテスティングのシナリオは、ソースコードと同じように、我々の大きな財産です。tests/以下に整理して管理しましょう。AATにおいて、どのシナリオを実行するかは、考えてやっていきたいですね。OSの機能性ごととか、アプリごとの回帰試験のシナリオとか。新規実装した場合は、シナリオテストの合格を目指す、テスト駆動を取り入れるとか。リグレッションの範囲を考えてシナリオを選んだり、あるシナリオ集合に名前をつけてたまにフル回帰テストをしたり。」「plan/tests.md にまとめるのがいいですね。」
- 2026-10-13（AskUserQuestion）p005 の起動の方法「ユーザーが USB で起動」（AAT の image を USB に焼いて 1 回起動、T1 が SSH で操作、ESP には書かない）
- 2026-10-27「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」

## 結果

- kernel の `/dev/input-inject` のマウスとキーボード（試験の kernel の設定だけ）、compositor の試験の image だけの撮影の口と `keiland-shot`、`aat-input`、host の道具 [plan/tools/aat/](../tools/aat/README.md)（click・drag・wheel・key・type・shot・wait-log、AAT の image の config、シナリオの選択の helper と suite の runner）、シナリオの置き場 `tests/scenarios/`（[plan/tests.md](../tests.md)、[tests/README.md](../../tests/README.md)）が main にある。
- 証拠: QEMU の T1-200（aat-p002）・T1-200c（host の道具）・T1-202c（smoke 8 本 pass、full 79 本の判定の一覧）、その後の AAT の実行（T1-232・T1-305 ほか）で使われている。

## 制限・移管

- p005（素の 5330 での AAT の実行と結果の記録）は実機で行った記録が無い（未実施）。2026-10-27 のユーザーの規則で WS を閉じた。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws173-p001 | kernel: `/dev/input-inject` にマウスとキーボード（directory は p002 と共有） | cleared（T1-200） |
| ws173-p002 | compositor: 試験の image だけの撮影の口と `keiland-shot` | cleared（T1-200） |
| ws173-p003 | host の道具 `plan/tools/aat/` と AAT の image の config | cleared（T1-200c） |
| ws173-p004 | AAT の項目の一覧と判定の基準 | cleared（T1-202c） |
| ws173-p005 | 素の 5330 で AAT を行い結果を記録（directory なし） | planned のまま WS を閉じた（実機は未実施、2026-10-27 のユーザーの規則） |
| ws173-p006 | シナリオの選択の helper（git の範囲 → paths）と suite の runner | cleared（T1-202c・T1-232・T1-305） |

## 記録の所在

Phase の記録と試験は削除する（Q1）。git の commit `fb1f6afee` の `plan/ws173/` にある。plan/tools/aat/README.md の自己試験が使う QEMU の image の config と `aat-p002.sh` は plan/tools/aat/ へ移す。Queue の履歴は [plan/history](../history/index.md)。
