<!-- awesome-plan project=zedbsd record=ws184 -->

# WS184: 左手デバイスの OSK（クリエイターモード）

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

「画面左上からの右下へのスワイプ操作は、現在はホーム画面遷移に割り当てられているが、別な操作に割り当てたい。画面右上スワイプと同じようなアニメーションでオンスクリーンキーボードを引き出して、いわゆる「左手デバイス」のOSKにしたい。」
「左手デバイスは、左から出るが、その後は左側、右側、下側、上側に移動できる。」
「左手デバイスのクリエイターモード ― 回転ダイヤル(カチカチという音でフィードバック) ― 垂直ホイール(カチカチという音でフィードバック) ― ボタンが2列x5個 ― Waylandのinputイベントを投げる」

- 画面の左上から右下への swipe で、右上の swipe（OSK）と同じ animation で左手デバイスが左から出る。出た後は左・右・下・上の端へ移せる。
- 部品: 回転ダイヤル、垂直ホイール（どちらもカチカチの音）、ボタン 2 列 × 5 個。
- 操作は Wayland の input の事象として app に届く（どの protocol・事象にするか（key・axis・tablet の pad など）は p001 で決めてユーザーに確かめる）。
- 今 App Home の入口の左上の swipe は [ws181-p008](../ws181/phase008/phase.md) で整理する。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| p001 | 設計: 引き出しの gesture と animation、配置と移動、部品と音、Wayland の事象（protocol の選び方、app の対応）、既存の OSK（WS102）との共通部分 | planned | — |
| p002 | 実装と QEMU・実機の確認 | planned | p001 |
