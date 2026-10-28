<!-- awesome-plan project=zedbsd record=ws084 -->

# WS084: i915 の firmware の画面の引き継ぎ（素の実機の UEFI の起動でデスクトップを出す）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: 目標（素の 5330 の UEFI の起動で desktop）は p002（2026-09-29 ユーザー「完璧です。」）と p003（2026-10-13 ユーザーの実機の確かめ「OKでした」、10 回の起動）で達成。p004（旧 takeover の parity の乖離）は WS245 p036・p037・p050a で takeover を xd に書き直し旧 file を削除したので対象が無くなった）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: —
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

素の 5330 を UEFI で起動すると、GOP が内蔵の LCD（pipe A）を点けたまま kernel に渡す。i915 の N0 の判定は active な pipe を「takeover（N1）が未移植」として止め、compositor は `ENOTSUP` で終わっていた。QEMU の passthrough には firmware の画面が無いので、この経路は試されていなかった。完了の条件: 素の 5330 を USB の image から UEFI で起動し、firmware の画面を引き継いで greeter（または自動の login の desktop）が内蔵の LCD に出る。手引きは [guide.md](guide.md)。

## ユーザーの決定

- 2026-09-29: 「ではWSを立ち上げて実装してください。display takeoverは以前に実験して動いた実績があり、難しくないと思います。メインエージェントで実装してください。」
- 2026-09-29: 「HDMIはいったんやめて、LCDのみの構成にします。その上で、過去にLCDのtakeoverに成功しているはずです。修正を続けてください。」（HDMI は WS075 の範囲に戻した）
- 2026-09-29: 「フリーズしていて、ネットワークも届きません」「LCDはGOPのまま有効でフリーズしてます。グラフィックブートを無効にするのがいいかもね。」
- 2026-09-29: 依頼「過去に動作したN1と、現在のコードを、レビューで解離がないか確認」。
- 2026-09-29: 「起動しました！ですが、1fpsくらいしか出ないです。」「マウスの移動をしても描画が1秒後ですね。…QEMUでパススルーして、マウス移動から描画までの時間を計ったりできますか？」→ 直した後「完璧です。」
- 2026-10-13: p003 の実機の確かめ「OKでした」。「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」
- 2026-10-27（q1608）: 「…masterとWSを修正して、completedやcanceledにしてください。」

## 結果

- p001（2026-09-29 main）: N0 は active な pipe で止まらず `takeover` の印を付け、resident の display の開始が最初の書き込みの前に N1（readout・sanitize・`intel_crtc_disable_noatomic`・release）を走らせる。
- p002（2026-09-29）: 素の 5330 の dmesg で見つかった組み込みの不足（PLL の bind が無く sanitize が firmware の DPLL1 を止める等）を直し、firmware の画面から LCD の desktop へ引き継いだ。1 fps の遅さも直し、操作中 24.5 present/s（10.0.30.5 の dmesg）。ユーザー「完璧です。」
- p003（2026-10-13）: 素の 5330 で demo の image の起動 10 回、ユーザーの確かめ「OKでした」。
- これらの takeover の code は後に WS245（p036・p037・p050a）で `xd` に書き直された（旧 file は削除）。

## 制限・移管

- p004（旧 parity の N1 との乖離 1〜3 の記録と直しの案）: 表・案の patch 2 つ（未適用）・host 試験まで。対象の旧 file が WS245 で無くなったので取り下げ。
- HDMI の主出力は WS075。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws084-p001 | N0 が active な pipe で止まらず N1 の takeover を走らせる | 実装（2026-09-29 main）、実機の確かめは p002 で |
| ws084-p002 | bare metal の log で見つかった組み込みの不足を直す | cleared（2026-09-29、素の 5330 で LCD の desktop） |
| ws084-p003 | L2: 素の 5330 で起動 10 回中 10 回 desktop | cleared（2026-10-13 ユーザーの実機の確かめ「OKでした」） |
| ws084-p004 | parity の N1 との乖離の記録と直しの案 | 取り下げ（in-progress のまま WS を閉じた。対象の旧 file は WS245 で削除） |

## 記録の所在

Phase の計画・結果・試験は削除する。p001・p002 の詳しい記録（旧 ws.md の 16 行目以降）は [bare-metal-record.md](bare-metal-record.md) に行の番号を変えずに残した。git の commit `fb1f6afee` の `plan/ws084/` にある。Queue の履歴は [plan/history](../history/index.md)。
