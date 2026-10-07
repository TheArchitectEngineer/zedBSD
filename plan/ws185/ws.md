<!-- awesome-plan project=zedbsd record=ws185 -->

# WS185: ゲームパッドの OSK とゲームコンソールモード（Arcade モード）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: —
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Target: **ベータ3**（2026-10-07 ユーザー「下記をベータ3に移動します。・左手デバイスOSK、ゲームパッドOSK, 写真の続き, カレンダーの続き, IMEの続き、POSIX, NVMe, make, RTL8822C, Sleep」）
Resume point: p001 の設計から。
<!-- awesome-plan-current:end -->

## 目標（2026-10-07 ユーザー）

「ゲームパッドのOSK(Arcadeモード) ― 画面の右上と左上を同時にスワイプすると、コンポジタがゲームコンソールモードになる。― 左右両側にOSKが出て、Xboxのゲームパッドをエミュレートする。― 左右アナログスティック ― ４方向ボタン ― 左右アナログトリガー ― ゲームコンソールモードはドックを非表示にして左右にOSK、専用のゲームシェルを起動。― シェルはFreeDesktopのアプリ定義ファイルからGames;のもののアイコンを一覧表示して、スワイプやゲームパッドで切り替えて決定して起動できるものがあればいい。― とりあえずシェルなしでmviewを起動すればいいと思う」

- 画面の右上と左上を同時に swipe → compositor がゲームコンソールモードへ。dock を隠し、左右に OSK、専用のゲームシェル。
- OSK は Xbox のゲームパッドを模す: 左右のアナログスティック、4 方向ボタン、左右のアナログトリガー（A/B/X/Y・bumper・start/back は設計で確かめる）。
- 模した gamepad の届け方（evdev の仮想の gamepad の device か、Wayland の事象か）を p001 で決める。
- 段 1: シェルなしで mview を起動する。段 2: シェルが .desktop の `Categories=Games;` の icon を一覧し、swipe・gamepad で選んで起動。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| p001 | 設計: 両上隅の同時 swipe、モードの出入り、OSK の配置と部品、gamepad の届け方、mview の起動 | planned | — |
| p002 | 段 1 の実装（モード・OSK・mview）と確認 | planned | p001 |
| p003 | 段 2: ゲームシェル（Games; の一覧と起動） | planned | p002 |
