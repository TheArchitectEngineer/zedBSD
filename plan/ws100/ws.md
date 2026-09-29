<!-- awesome-plan project=zedbsd record=ws100 -->

# WS100: system bar の音量（icon・slider・確かめの音）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（設計）から。基準の案はユーザーの確認待ち
<!-- awesome-plan-current:end -->

## 目標（2026-09-30 ユーザー）

「音声については、サウンドの音量を右上の通知領域にアイコンとして追加し、ボリュームを調整できるようにしたいです。また、動画再生はOSCデモの
あとで実装するとして、ボリューム調整のフィードバックの音だけは鳴るとうれしいです。」

## 達成基準（案、2026-09-30 main。ユーザーの確認待ち）

| # | 基準 | 確かめ方 |
| --- | --- | --- |
| A1 | system bar の右上の通知領域に音量の icon があり、今の音量（無音・小・中・大）を絵で示す | QEMU の画面 |
| A2 | icon を click（touch では tap）すると slider の popup が出て、drag で 0〜100% を変えられる。mute の切り替えがある。popup の外で閉じる | QEMU の自動の試験 |
| A3 | icon の上の wheel で音量が 5% ずつ変わる | QEMU の自動の試験 |
| A4 | 音量を変えるたびに短い確かめの音（約 100 ms）が、その音量で鳴る。連続の操作では重ならない | QEMU（`intel-hda` と `hda-duplex` を wav に録る）、実機はユーザーの耳 |
| A5 | 音量は再起動の後も保たれる（利用者の設定の file） | QEMU の自動の試験 |
| A6 | audiod が無い、または音の device が無いときは、icon が「音なし」の印になり、popup にその旨を出す。落ちない | QEMU（audio の device なし） |
| A7 | 5330 の実機の内蔵の speaker と headphone の端子から音が出る（HDA 8086:51c8、Alder Lake-P） | 実機（ユーザー） |

動画の再生と、app ごとの音量は範囲の外（デモの後）。

## 前提と危険

- 音の経路は audiod（ws035-p009・p049・p050、unix socket と共有メモリ）と `src/drivers/pci/pci-hda.c`。QEMU の起動の log は `audiod: no device`
  （今の試験の QEMU に音の device が無い）。
- **5330 の音の controller は Alder Lake PCH-P の HDA（8086:51c8、Dell 1028:0b02）**。Linux は SOF（DSP）か legacy の HDA で扱う。legacy の HDA の
  経路で内蔵の codec（analog の speaker・headphone）が鳴るかは未確認で、鳴らなければ DSP の firmware が要る恐れがある（A7 の危険、p001 で調べる）。
  内蔵の DMIC は範囲の外。
- 音量の設定は WS089 の desktop の設定の file（`keiland_preferences_*`）に置くか、audiod が持つかを p001 で決める。Settings の Sound の頁（今は表示だけ）
  との関係も決める。

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws100-p001 | 設計: system bar の icon と popup（zdesktop）、audiod の音量の interface、確かめの音、設定の保存、QEMU の音の試験の方法、5330 の HDA の経路の調べ（codec の有無、legacy の HDA で鳴るか） | planning | — |
