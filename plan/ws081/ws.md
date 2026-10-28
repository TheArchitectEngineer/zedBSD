<!-- awesome-plan project=zedbsd record=ws081 -->

# WS081: touch の操作の質（慣性のある scroll と、低い fps の touch の補間）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: 慣性の scroll と touch の補間の library と全 app への適用は p001〜p006・p010〜p016・p018・p019 cleared（main）。残りの L3（p017 の計測・p007 の実物の調整）は 2026-09-30 午後 ユーザー「では、いったん描画の高速化はラップアップして…」で優先を下げた物）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

1. **慣性のある scroll**（指を離した後も速度に応じて減速しながら続く、端での戻り）を、touch と touchpad で、ブラウザ・Files・Terminal・PDF Viewer 等で
   一貫した手触りにする。
2. **低い fps の touch の補間**: 報告の間隔が長い（例: 60 Hz 以下、不揃い）安い touch panel でも、数式による補間・予測（速度の推定、平滑化、
   表示の frame の時刻への resampling と短い外挿）で、scroll・drag・Notes の線が滑らかに見えるようにする。
3. 担当の層を 1 か所で設計する: HID の driver（正確な時刻の付与、報告の率の測定）、compositor（resampling・予測・gesture の判定・app への渡し方）、
   app（ブラウザ・Keiland の app の慣性の scroll）。

設計は [design.md](design.md)、手引きは [guide.md](guide.md)。

## ユーザーの決定

- 2026-09-28: 「タッチについては、ただマウスのように操作するのではなく、スクロールの操作みたいに余韻のあるやつとかも実装が必要で、これはHIDドライバ、
Waylandコンポジタ、ブラウザの3つに渡る実装と調整が必要な作業だと思います。計画は1カ所でやるのがいいですね。また、タッチのfpsが低い廉価な機種でも、
ある程度数式で補間して利用できるようにすることを目標に入れましょう。iPadみたいに120Hz駆動のタッチは、安い端末にはついてませんからね。」
- 2026-09-29（Notes の指、実機の AES ペンがまだ認識されていない件で）: 「切り替えを付ける」→ 既定は指で scroll・pinch（掌の誤りの線を防ぐ）、toolbar の切り替えで一本指で線を引ける（その間は二本指で scroll・pinch）。p015。
- 2026-09-29（main の判断、design §8・§11）: p002 の範囲の変更、library の置き場所、p005 を scroller・gesture の library と app ごとの適用に分ける、p006 は WS074 に依存。
- 2026-09-30: デモの touch は Windows 上の QEMU（WS085）で行う。外付けの touch LCD は間に合えば別の計画。
- 2026-09-30 午後: 「では、いったん描画の高速化はラップアップして…」（L3 の優先を下げた）。
- 2026-10-13: 「これ以上ベータ2に機能追加しないつもりなので、実装を終わらせましょう」（p017 の q957）。
- 2026-10-27（q1608）: 「…masterとWSを修正して、completedやcanceledにしてください。」

## 結果

- kernel: 1 報告 1 時刻（URB の完了の時刻）と Scan Time の `EV_MSC`/`MSC_TIMESTAMP`、注入の device と touchinject の Scan Time（p002）。PS/2 の mouse・touchpad の wheel（p019、BUG-156、T1-015）。
- 補間・予測の library（`userland/desktop/libkeiland/motion.c`、p003）、compositor の resampling・予測と wl_touch の時刻（p004）、慣性の scroll と gesture の共通の library（scroller・gesture、p005）、指の drag and drop（p014）。
- app への適用: ブラウザ（p006）、Files（p010）、Terminal（p011）、PDF Viewer（p012）、Notes（p013、指で書く切り替えは p015）。
- L1: Linux の QEMU の注入の touch で、tap・double tap・長押し・慣性の scroll・pinch が 6 app で動いた。L2 の準備（p016: touchlog と集計、Windows の手順）と Windows の QEMU 用の demo の image（p018）。
- 証拠は QEMU（Venus、注入の touch）と host。

## 制限・移管

- L2 の Windows の物理の touch panel での計測（ユーザーの操作）と L3（p017: 反応の時間・慣性の frame の間隔の計測、試験の file と image は済み、QEMU の確かめと Windows の計測は未実施）、p007（実物での係数の調整）は未実施。優先を下げた（2026-09-30 午後）。
- touchpad の二本指の scroll は design §10 の 4 のユーザーの判断の後（この WS では Phase を置かなかった）。
- BUG-099 は ticket で。規約の全文（p009）は WS の外の一括の見直しへ。
- 試験（`tests/`）は Notes・Terminal の touch の host 試験（tools.md 登録済み）・Windows の demo の image・PS/2 の wheel の試験などとして使われるので `plan/tools/` へ移す（Q1 の OK の後）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws081-p001 | 設計（時刻・補間の数式・app への渡し方・慣性の物理・試験） | cleared（2026-09-29） |
| ws081-p002 | kernel: 1 報告 1 時刻と Scan Time | cleared（2026-09-29） |
| ws081-p003 | 補間・予測の library | cleared（2026-09-29） |
| ws081-p004 | compositor の resampling・予測、library の公開 | cleared（2026-09-29） |
| ws081-p005 | 慣性の scroll と gesture の library | cleared（2026-09-29） |
| ws081-p006 | ブラウザの慣性の scroll と touch | cleared（2026-09-29） |
| ws081-p007 | touch の実物での調整 | planning のまま WS を閉じた（未実施） |
| ws081-p009 | 全文規約確認と回帰 | planning のまま WS を閉じた（WS の外の一括の見直しへ） |
| ws081-p010 | Files への適用 | cleared（2026-09-29） |
| ws081-p011 | Terminal への適用 | cleared（2026-09-29） |
| ws081-p012 | PDF Viewer への適用 | cleared（2026-09-29） |
| ws081-p013 | Notes の指の操作と掌の判定 | cleared（2026-09-29） |
| ws081-p014 | 指の drag and drop | cleared（2026-09-29） |
| ws081-p015 | Notes の「指で書く」の切り替え | cleared（2026-09-29） |
| ws081-p016 | L2 の準備（touchlog・集計・Windows の手順） | cleared（2026-09-30） |
| ws081-p017 | L3 の計測 | in-progress のまま WS を閉じた（試験の file と image は済み、計測は未実施） |
| ws081-p018 | Windows の QEMU 用の demo の image | cleared（2026-09-30） |
| ws081-p019 | BUG-156: PS/2 の wheel | cleared（2026-10-03 Q1、T1-015） |

## 記録の所在

Phase の計画・結果は削除する。git の commit `fb1f6afee` の `plan/ws081/` にある。Queue の履歴は [plan/history](../history/index.md)。
