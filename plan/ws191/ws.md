<!-- awesome-plan project=zedbsd record=ws191 -->

# WS191: 再生の音を libkeiland の audio stream の口へ（compositor・backend、3 OS）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001〜p005 cleared（kl_audio_、zedBSD・Linux・FreeBSD の backend、videoplayer・music の移行））
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: MG007
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

- libkeiland に音の stream の口（開く・形式・書く・止める・遅れの問い合わせ、公開 API は kl_audio_）を作り、compositor・libkeiland-backend が OS ごとに流す: zedBSD は audiod、Linux は alsa-lib を dlopen した PCM の default、FreeBSD は OSS。
- videoplayer・music・libmedia をその口に移し、境界の検査の許可の表の PENDING の行を外す。
- 遅れ（A/V の同期）と大きなデータの受け渡し（shm）を compositor を通しても保つ。

設計は [design.md](design.md)。

## ユーザーの決定

- 2026-10-08 昼（クリックの回答、由来）「libkeilandのaudio streamの口に移す」（WS188 p003 の検査で videoplayer・music・libmedia が audiod に直に流していると分かった後。Guardrail「Bluetooth と Display も compositor 経由」と同じ規則を音にも）。
- 2026-10-08 午後「サウンドはlibkeiland-backendに入れてください。libkeilandのAPIはkl_audio_がいいです。」
- 2026-10-08 午後（H2）Linux の再生の経路は alsa-lib を dlopen、無ければ stream を断る。

## 結果

- libkeiland の kl_audio_stream、compositor の中継、zedBSD の backend（audiod）、Linux（alsa-lib の dlopen、memfd の ring）・FreeBSD（OSS）の backend を実装し、videoplayer・music を移した。libmedia から音を外した（main）。
- QEMU: T1-444 PASS（Music・Video Player の音が kl_audio_stream を通り WAV に音、pause で無音、seek で flush）。T1-448 FreeBSD・T1-454 Linux PASS。

## 制限・移管

- T1-444 の注「Video Player の時計が wall の約 2 倍に見えた」はその後の確かめの記録が無い（未確認、ticket なし）。
- 実機 5330 で音が出ることのユーザーの UAT は記録が無い（未確認）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws191-p001 | 設計（stream の口、中継か fd の受け渡しか、遅れ、3 OS）、design-reviewer | cleared（2026-10-08、第 4・5 版で blocking 0） |
| ws191-p002 | libkeiland・compositor・zedBSD の backend | cleared（2026-10-08、T1-444） |
| ws191-p003 | app の移行（videoplayer・music）、libmedia から音を外す | cleared（2026-10-08、T1-444） |
| ws191-p004 | Linux・FreeBSD の backend | cleared（2026-10-08、T1-448・T1-454） |
| ws191-p005 | 規約の全文の見直し | cleared（2026-10-09） |

## 記録の所在

Phase の計画・結果・試験は削除した。git の commit `fb1f6afee` の `plan/ws191/` にある。Queue の履歴は [plan/history](../history/index.md)。
