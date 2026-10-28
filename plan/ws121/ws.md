<!-- awesome-plan project=zedbsd record=ws121 -->

# WS121: Web ブラウザでのアクセラレーションつきのビデオ再生

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。p001・p002・p004〜p006 cleared（2026-10-07 T1-315: libmedia、`<video>`、JS の HTMLMediaElement、音と controls）。H.264 の hardware decode の土台は WS083（Vulkan Video）、native の再生は WS202 で main に統合）
<!-- retained-after-completion --> Phase phase001・phase004 はその場に残す: src/・userland/・platform/・Makefile・config/ の source か build の file が path を名指す（AGENTS.md §10 の source の comment の掃除の後に消す。2026-10-28 Q1 の方針、q1660）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

2026-10-02 ユーザー（ベータ1、リリース目標 10/17）「Webブラウザでアクセラレーションつきのビデオ再生を可能にする。」

browser で `<video>` を再生する。decode は libmedia が持ち、Vulkan Video（WS083）の hardware decode を使える形にする。

## ユーザーの決定

- 2026-10-02: 動画関連（WS083・WS121・WS122）は別セッションでユーザーがノウハウを提供しステップバイステップで進める。ベータ1 では最悪 drop してもリリース可能（努力目標）。VA-API（WS123）は canceled、アプリが Vulkan Video を直接使う。
- 2026-10-05「WS107, WS121はあなたが作業します。」→ Codex の担当を外し、Q1 の側で p001 の設計から。
- p001 の判断 U0〜U7 は既定で進めた（2026-10-08 Q1 の判定）。
- 2026-10-08「規約の整形（各 WS の全文規約の Phase）はベータ3」→ 最後の全文規約は WS の外の一括の見直しへ。

## 結果

- libmedia（mediafile の source、engine、共有の library、p002）、`<video>` の DOM・layout・描画と autoplay muted（p004）、JS の HTMLMediaElement と event（p005）、音と controls の帯・AAT（p006）が main にある。
- 証拠: T1-315 の AAT browser.video（`MEDIA play position_ms=0`・`pause position_ms=5083`）と playing の PNG（QEMU）。
- libmedia の Vulkan Video の decoder（`vkvideo.c`）は WS083・WS202 の成果。

## 制限・移管

- p003（Range の loader と media-fetch、大きな remote の media）は着手せず。今は loader で全体を 64 MiB まで。browser 本体は WS074。
- 全文規約と回帰は WS の外の一括の見直しへ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws121-p001 | 要件・設計（`<video>`、libmedia、Range、JS の API、音） | cleared（2026-10-08 Q1 の判定、U0〜U7 は既定） |
| ws121-p002 | libmedia | cleared（2026-10-07、T1-315） |
| ws121-p003 | Range の loader と media-fetch | 着手せず（Phase の directory なし） |
| ws121-p004 | `<video>` の DOM・layout・描画 | cleared（2026-10-07、T1-315） |
| ws121-p005 | JS の HTMLMediaElement と event | cleared（2026-10-07、T1-315） |
| ws121-p006 | 音と controls の帯、AAT | cleared（2026-10-07、T1-315） |
| （最後） | 全文規約と回帰 | WS の外の一括の見直しへ |

## 記録の所在

Phase の計画・結果・試験（`phaseNNN/`・`tests/`）は削除する（Q1 の pipeline）。git の commit `fb1f6afee` の `plan/ws121/` にある。Queue の履歴は [plan/history](../history/index.md)。
