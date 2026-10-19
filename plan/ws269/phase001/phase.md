<!-- awesome-plan project=zedbsd record=ws269-p001 -->

# ws269-p001 PowerVR BXE-4-32 の driver の設計

- 親: [ws269](../ws.md)、Queue: q1316 / q1316-i01（P2、2026-10-19）
- Status: in-progress（設計の文書が出来た、design-reviewer 待ち。2026-10-19 P2、[design.md](../design.md)）

## 目的

JH7110 の PowerVR BXE-4-32 を zedBSD で動かす道を決め、`plan/ws269/design.md` に書く。コードは書かない。

## 受け入れの条件

1. 公開の資料（Linux の mainline の `drivers/gpu/drm/imagination/`、Mesa の `src/imagination/`、JH7110 の TRM・DT binding、Imagination の firmware の配布）を調べ、BXE-4-32 が mainline・Mesa でどこまで対応しているかを出典付きで書く（未確認の点は未確認と書く）。
2. kernel の driver の構成（電源・clock・reset は WS256 の p013 の基盤、MMU・firmware の起動・command の queue、/dev の UAPI）、firmware の入手と license、userland の Vulkan の道（Mesa の powervr を移植するか）を案と比較で書き、推奨を一つ示す。
3. 実装の Phase の分割（依存・LW の見込み・T1 と実機の確認の点）と、ユーザーの判断が要る点（D1…）を挙げる。HAL の API の変更が要るなら差分の案を proposed/ に置くだけにする。
4. GPL の source は手順の参照だけ（2026-10-19 ユーザーの許可: ISC の source の無い VF2 の部品は GPL の手順を参照してよい）。code を写さない。

## 設計の確かめ

設計が出来たら Q1 が design-reviewer（Opus 5.5）に回す。
