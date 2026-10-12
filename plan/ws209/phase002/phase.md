<!-- awesome-plan project=zedbsd record=ws209-p002 -->

# ws209-p002: audiod の endpoint・route・権限・protocol 版 2

Parent: [WS209](../ws.md)
Status: in-progress（P1。制御の面（protocol 2・endpoint・route・権限・event・hold・音量の行き先）と host 試験まで。音を外の endpoint へ流す面は p003）
Phase disposition: normal
Queue: P1 の lane

## 範囲と設計

[p001](../phase001/phase.md) の設計 1.1・1.2（Q1 確認済み）の E・R・P・N の制御の面。ASRC・水位・外の endpoint の ring の読み書き・call の脚の mix は p003、desktop の口（libkeiland・compositor・3 OS・Settings・Phone）は p004。

## 実装

| 所 | 内容 |
| --- | --- |
| `userland/base/audiod/protocol.h` | `AUDIOD_PROTOCOL_VERSION_1`・`_2` と `AUDIOD_SHM_VERSION` 1 に分けた（P1'、`AUDIOD_VERSION` は消した、N9）。新しい request 12〜23・event 73〜79、SUBSCRIBE の bit（VOLUME・ENDPOINTS・ROUTES）、kind・class・state・route・reason・role の定数、`audiod_stream_create2`（48 byte、role、N11）、`audiod_endpoint_add`・`_info`（128 byte）、`_added`・`_state`・`_wanted`・`_removed`・`_volume`、`audiod_route_set`・`_info`、`audiod_call`、`audiod_media_held`。`AUDIOD_MESSAGE_MAX` 256（N18）。`_Static_assert` で大きさ |
| `userland/base/audiod/route.c`（新） | endpoint の登録（root・`_bluetooth` だけ、E3。同じ key と kind の古い物は消す、N15。id は 16 から使い回さない、N14）、ring（`audiod_shm_make`）、state と ticket（ACTIVE はその ticket だけで open、N2）、FAILED と期限（headset-media 2 秒・他 3 秒、route は変えず TIMEOUT、N1）、「使う」（media の再生と 5 秒の猶予、録音、call、N8）、route の枠ごとの class（R6'・M2）、ROUTE_SET（root・`_bluetooth`・seat の持ち主）、CALL_SET（自分の phone・modem の group、sink と source、2 つ目は EBUSY）・CLEAR・STATE（記録して知らせる、G3・N12）、出力が消えたら media の hold（G5: 出力は無し、STOP→START か MEDIA_RELEASE まで）、call の利用者の側が消えたら既定へ、far が消えたら call を外す、音量の行き先（call の間は call の出力、外の endpoint は自分の音量と daemon への ENDPOINT_VOLUME・REPORT、初めは 50%、N5）、通話中の録音は seat・root だけ（N13）、event は版 2 の subscriber だけ |
| `userland/base/audiod/main.c` | HELLO の版の交渉（版 1 は新しい request が EINVAL、SUBSCRIBE は VOLUME の bit だけ、N10）、WELCOME の device = 出力がある（全部の版、N19）、peer の uid（getpeereid、host 試験は SO_PEERCRED）、接続の 8 を root の枠に（N23）、STREAM_CREATE の role、START で hold を解く、DEVICE_VOLUME は route の行き先へ、poll の待ちに endpoint の期限、毎回 `audiod_route_tick`、client が去ったら endpoint と call を外す、ring の作りを `audiod_shm_make` に、host 試験の socket（`AUDIOD_HOST_TEST` の時だけ） |
| `userland/base/audiod/mix.c` | hold の stream と、出力が device でない media の stream を読まない（alert は device へ）。外の endpoint へ流すのは p003 |
| 版の名前の追従 | `userland/tests/audio-loop/main.c`、`libkeiland-backend-zedbsd/audio-zedbsd.c`・`audio-stream-zedbsd.c`（HELLO は `AUDIOD_PROTOCOL_VERSION_1`、assert は `AUDIOD_SHM_VERSION`） |

## 確かめ（2026-10-11 P1、host）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws209/tests/run-audiod-route-test.sh`（新、audiod を host で ASan/UBSan 付きで build） | all PASS ×2（本筋と拒否）: 版 1 の WELCOME と版 2 の request の EINVAL、subscribe、endpoint 6 つの登録と INFO、WELCOME の device が出力の登録で 1 に、枠の強制（phone は media の出力・利用者の入力にならない、headset-media は call の出力にならない）、route の INFO、media の stream の開始で WANTED ticket 1、ticket の ACTIVE、2.5 秒 TIMEOUT 無し、応えない headset は 2 秒で TIMEOUT・route はそのまま・WANTED 0、出力の削除で MEDIA_HELD（OUTPUT_LOST）と出力 0、MEDIA_RELEASE、CALL_SET と 2 つ目の EBUSY、call の既定が headset の voice（G1）、音量の key が call の出力へ（daemon に ENDPOINT_VOLUME 30、REPORT 70 が seat に）、daemon が去ると call が外れる。拒否: 別の uid の ENDPOINT_ADD・ROUTE_SET は EACCES |
| zedBSD の audiod・audio-loop・libkeiland・wayland の build | rc 0、warning 0 |
| Linux の keiland の build | rc 0、warning 0 |
| style-check | route.c と試験は指摘 0、main.c・mix.c は私の変えた行の指摘を直した（旧い行は旧い形のまま） |

## 未実施・残り

- QEMU（p005 で T1）。
- p003: 外の endpoint の ring の読み書き（E6 の位置の検査・SIGBUS の扱い、N16 の書き手）、ASRC と水位（C1〜C5、M8・M9）、call の脚と Q13 の −12 dB・speaker の時の media の停止（R5'）、SUSPENDED の待ち（N20）、played_position（N4）、外の sink の soft volume。
- p004: desktop の口（N6・N17）。
