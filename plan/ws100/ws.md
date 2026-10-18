<!-- awesome-plan project=zedbsd record=ws100 -->

# WS100: system bar の音量（icon・slider・確かめの音）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」。全 Phase が cleared。旧: incomplete、残りは p006（5330 の HDA、実機））
<!-- retained-after-completion --> Phase の directory・試験は他の WS・plan/tools から参照されているので残す（2026-10-18 Q1、ユーザーの承認の案 (a)）。参照する試験を変える時に 2026-10-06 の基準で plan/tools へ移すか消す。
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

ユーザー（2026-09-30）「音声については、サウンドの音量を右上の通知領域にアイコンとして追加し、ボリュームを調整できるようにしたいです。また、動画再生はOSCデモのあとで実装するとして、ボリューム調整のフィードバックの音だけは鳴るとうれしいです。」
達成基準 A1〜A7（同日朝ユーザーが「案のまま確定」）: system bar の音量の icon と slider の popup・wheel・mute、確かめの音（約 100 ms、重ならない）、再起動の後も保つ音量、音の device が無い時の扱い、5330 の内蔵の speaker と headphone（A7、デモに必須ではない）。Settings の Sound の頁（p005）も範囲に入れた。

## 結果（2026-10-13 完了）

- audiod: 内蔵の確かめの音（`AUDIOD_FEEDBACK`）と、amplifier の無い device の software の音量（p002）。音量の曲線（25・50・75・100% → -30・-15・-7・0 dB ± 3 dB、p009、T1-478・T1-505）。
- libkeiland の `keiland_audio_*`（p003）、zdesktop の system bar の icon・popup・wheel・確かめの音（p004、A1〜A6 PASS）、Settings の Sound の頁の slider と mute（p005）。
- 確かめの音の遅れ（L3）: QEMU の guest の中の経路で 50 ms 以内（p008、2026-09-30 Q1 の判断）。
- Bug の直し: BUG-153（click が読み戻しで戻る、p010）、BUG-161（操作のたびに desktop.conf へ書かない、session の終わりに一度、p012）、BUG-170（slider のドラッグでのフリーズ、確認の音は離した時に 1 回、p013）。
- 実機: 5330 の HDA で鳴る（p006、2026-10-13 ユーザーの実機の確かめ「OKでした」）。

## 制限・移管

- app ごとの音量、出力の選択（headphone・HDMI）、動画の音は範囲の外（L4、デモの後）。
- 確かめの音の遅れの合否は QEMU の guest の中の経路で決めた。実機の操作から音までの時間の計測は未実施（再検討のきっかけ: 実機で 50 ms を超えたとき、kernel の audio の fragment の縮小）。
- 規約の全文の見直し（p007）は WS100 で変えた関数とその周りだけ。audiod の残りの 89 件は一覧のまま。
- 設計と手引きは [design.md](design.md)・[guide.md](guide.md)（source の注 `userland/desktop/wayland/volume.c` が design.md を指す）。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws100-p001 | 設計（icon と popup、audiod の音量と確かめの音、保存、QEMU の音の試験、5330 の HDA の調べ） | cleared（2026-09-30） |
| ws100-p002 | audiod の確かめの音と software の音量 | cleared（2026-09-30、audiod-qemu PASS） |
| ws100-p003 | libkeiland の `keiland_audio_*` | cleared（2026-09-30、host 14/14） |
| ws100-p004 | zdesktop の system bar の音量（icon・popup・wheel・確かめの音・保存） | cleared（2026-09-30、volume-p004 PASS） |
| ws100-p005 | Settings の Sound の頁（slider と mute） | cleared（2026-09-30、volume-p005 PASS） |
| ws100-p006 | 5330 の HDA で鳴らす（L2、A7） | cleared（2026-10-13 ユーザーの実機の確かめ「OKでした」、上の原文） |
| ws100-p007 | 規約の全文への合わせと全体の確かめ | cleared（2026-09-30） |
| ws100-p008 | L3 確かめの音の遅れの計測と短縮 | cleared（2026-09-30 Q1 の判断） |
| ws100-p009 | L3 音量の曲線 | cleared（2026-10-09 Q1、T1-478・T1-505） |
| ws100-p010 | BUG-153 の直し | cleared（2026-10-03 Q1、T1-018） |
| ws100-p012 | BUG-161 の直し | cleared（2026-10-03、T2-009） |
| ws100-p013 | BUG-170 の直し | cleared（2026-10-08 Q1、T1-087） |

Phase の directory とこの WS だけの試験は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。
