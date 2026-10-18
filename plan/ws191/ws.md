<!-- awesome-plan project=zedbsd record=ws191 -->

# WS191: 再生の音を libkeiland の audio stream の口へ（compositor・backend、3 OS）

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-13 P2 q964 の照合: p001〜p004 は各 phase.md で cleared（2026-10-08 Q1、T1-444・T1-448・T1-454）、残りは p005（規約、ベータ3）。ベータ2 の範囲は完了の見込み、判定は Q1）
Primary Milestone: MG006
Related Milestones: MG007
Objectives: O2
Parent: [Master](../master.md)
Queue: q895（P2、WS083 の後に回した 2026-10-08 午後）
<!-- awesome-plan-current:end -->

## 由来（2026-10-08 昼 ユーザー、クリックの回答）

WS188 p003 の検査で、動画（videoplayer/audio.c）・音楽（music）・libmedia が再生の音を zedBSD の audiod に直に流している（userland/base/audiod/protocol.h を include、AF_UNIX で AUDIOD_SOCKET_PATH、shm の ring）と分かった。ユーザー:「libkeilandのaudio streamの口に移す」（Guardrail「Bluetooth と Display も compositor 経由」の規則、サウンドも同様）。

## 到達目標

- libkeiland に音の stream の口（開く・形式・書く・止める・遅れの問い合わせ）を作り、compositor・libkeiland-backend が OS ごとに流す: zedBSD は audiod、Linux は alsa-lib を dlopen した PCM の default（普通は pipewire-alsa を通って PipeWire、ユーザーの決定 H2）、FreeBSD は OSS。音の出力は libkeiland-backend の中、公開 API は kl_audio_（ユーザー 2026-10-08 午後）。
- videoplayer・music・libmedia をその口に移し、境界の検査の許可の表の PENDING の行を外す。
- 遅れ（A/V の同期）と大きなデータの受け渡し（shm）を compositor を通しても保つ設計。

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計（stream の口、compositor の中継か fd の受け渡しか、遅れ、3 OS の backend）、design-reviewer | cleared（2026-10-08 Q1: 第 4・5 版の design-reviewer が blocking 0） | — |
| [p002](phase002/phase.md) | libkeiland・compositor・zedBSD の backend | cleared（2026-10-08 Q1、T1-444 PASS） | p001 |
| [p003](phase003/phase.md) | app の移行（videoplayer・music）、libmedia から音を外す | cleared（2026-10-08 Q1、T1-444 PASS） | p002 |
| [p004](phase004/phase.md) | Linux・FreeBSD の backend | cleared（2026-10-08 夜 Q1、T1-448 FreeBSD・T1-454 Linux PASS） | p002 |
| p005 | 規約の全文の見直し | cleared（2026-10-18 Q1: phase.md に合わせた。旧: planned（ユーザーの決定でベータ 3 に回す、2026-10-08 夜）） | p003・p004 |

## 2026-10-13 P2 q964 の照合

- 表の p001〜p004 を各 phase.md の Status（全て cleared、2026-10-08 Q1）に合わせた（表は in-progress・planned のまま古かった）。
- 到達目標（libkeiland の音の stream の口、compositor・backend が zedBSD の audiod・Linux の alsa-lib（dlopen）・FreeBSD の OSS に流す、videoplayer・music の移行と libmedia から音を外す）は p001〜p004 の証拠で満たされている。残りは p005（規約の全文、ユーザーの決定でベータ3）。
- 未確かめの観察（ticket なし）: p002・p003 の clear の注「Video Player の時計が wall の約 2 倍に見えた（未確認、P2 が確かめる）」は、その後の確かめの記録が無い。QEMU で確かめる要がある（T1 で Video Player の 20 秒の sample の再生の秒を測る）。直すか ticket にするかは Q1 の判断。
- master.md の WS191 の行（「planning」「p001 設計」）は古い（Q1 の投影）。
