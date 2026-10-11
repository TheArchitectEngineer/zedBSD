<!-- awesome-plan project=zedbsd record=ws209-p001 -->

# ws209-p001: audiod の音の endpoint と route — 再設計

Parent: [WS209](../ws.md)
Status: in-progress（P1。設計の第 1 版、design-reviewer の review を Q1 に依頼）
Phase disposition: normal
Queue: P1 の lane

## 由来と入力

- 2026-10-11 ユーザー: 通話の音は app が PCM を中継せず、出力先を指定する model（[ws197 の ws.md](../../ws197/ws.md) の末尾）。Q12〜Q14 は「推しで OK」: R は新しい WS（この WS209）。Q13 = 通話の間、出力が speaker なら media を止め、headset なら −12 dB で重ねる。Q14 = 「スマホで話す」の切り替えは Phone の通話の card だけ。
- 元の設計は [ws197-p007](../../ws197/phase007/phase.md) の設計 2 の R 節（R1〜R9）。review-2（[ws197-p007/review-2.md](../../ws197/phase007/review-2.md)）の R の指摘 6 つ（拍の model、R8 × A2DP の Suspend、OS の境界と権限、見積もり、protocol の大きさ、endpoint の寿命）と遅れの表（X4・S5・R7）をここで直す。review-3 には R の指摘は無い。
- 使う側: ws197-p007b1（HFP の通話の音: phone の SCO）、WS205 の p004（A2DP の sink と route `media`）・p009（headset の SCO と route `call`）、将来の modem。

## 今の tree の事実（2026-10-11、agent/p1）

| 所 | 事実 |
| --- | --- |
| audiod | `userland/base/audiod/`（main.c 695 行・mix.c 683・device.c 534・protocol.h 242）。thread は 1 つ、`poll` の loop。kernel の `/dev/dsp0` を 1 つ開き、mix の拍は device の period（fragment 4096 byte = 48 kHz・stereo・16 bit で 1024 frame、21.3 ms）。device が無い時は timer。先行は 2 fragment（`AUDIOD_FILL_FRAGMENTS`） |
| protocol | `AUDIOD_VERSION` 1、message は長さ付き、`AUDIOD_MESSAGE_MAX` 64 byte。音は socket を通らず、stream ごとの共有 memory の ring（`audiod_shm_header`、位置は frame で増えるだけ、64 bit の位置は sequence の語で i386 でも一つの値に読める）を SCM_RIGHTS で渡す |
| 変換 | stream ごとに線形補間（32.32 の phase と step）。anti-alias の filter は無い |
| socket | `/run/audiod.sock` は 0666（main.c:198）。peer の uid は見ていない |
| 権限の前例 | volumed は `getpeereid` で uid を取り、seat の持ち主（sessiond が `/dev/gpu0` を chown した uid）と root と greeter にだけ mount・eject を許す（volumed/main.c:746-752・1020-1033）。bluetoothd も `getpeereid` で uid を見る。bluetoothd の child の uid は `_bluetooth` |
| desktop の音 | compositor（`wayland/volume.c`・`audio-stream.c`）が `libkeiland-backend` の `kl_backend_audio_*`（音量・feedback）と `kl_backend_audio_stream_*`（app の stream の中継）を使う。zedBSD の実装は `libkeiland-backend-zedbsd/audio-zedbsd.c`・`audio-stream-zedbsd.c`、Linux は ALSA の mixer、FreeBSD は OSS の mixer、無い所は `libkeiland-backend/unsupported/` の stub |
| kernel の音の device | 5330 は HDA（WS204 で録音も）、Pi 4 は 3.5 mm の PWM（WS207 p002、再生だけ）。どちらも `/dev/dsp0` 1 つ |

## 設計

### 用語

- **endpoint**: 音の出入り口 1 つ。**sink**（audiod が書く: speaker、headset の A2DP、headset の SCO の送り、phone の SCO の送り）か **source**（audiod が読む: mic、headset の SCO の受け、phone の SCO の受け）。
- **group**: 一つの機器の endpoint の組（例: headset の voice の sink と source、phone の SCO の sink と source）。route は group と向きで endpoint を選ぶ。
- **route**: 名前のある配線。`media` と `call` の 2 つだけ。

### E: endpoint

| ID | 決め |
| --- | --- |
| E1 | **内の endpoint**: kernel の `/dev/dsp0` の再生が sink `device:out`（class speaker）、録音が source `device:in`（class mic、録音の無い device には無い）。audiod が持ち、消えない。名前は class と kernel の device の名で（例「内蔵スピーカー」は UI の側で訳す） |
| E2 | **外の endpoint**: daemon が `AUDIOD_ENDPOINT_ADD` で登録する。中身: `kind`（sink・source）、`class`（speaker・mic・headset-media・headset-voice・phone-call・modem-call）、`group`（daemon が付ける 32 bit の番号、同じ daemon の中で一意）、`format`・`channels`・`rate`（8000・16000・44100・48000）、`buffer_frames`、`target_frames`（水位の目標）、`state`（下の E4）、`name`（32 byte 固定、UTF-8、NUL で終わる）。答え `AUDIOD_ENDPOINT_ADDED {id}` に ring の共有 memory を SCM_RIGHTS で付ける（stream と同じ `audiod_shm_header` と位置の決まり。sink は audiod が write_position を進め daemon が read_position を、source はその逆） |
| E3 | **登録の権限**: peer の uid（`getpeereid`）が root か `_bluetooth` の接続だけ（将来の modem の daemon の uid は、その WS で表に足す）。他は `AUDIOD_ERROR EACCES` |
| E4 | **状態**: `ACTIVE`（流せる）・`SUSPENDED`（一時止まり: 例 通話の間の A2DP の Suspend。route の選びは残り、その endpoint への音は捨て、そこからの音は無音）・`DORMANT`（今は開いていないが、route が選べば daemon が開ける: 例 繋がっているが SCO の無い headset の voice、Start の前の A2DP）。daemon が `AUDIOD_ENDPOINT_STATE {id, state}` で変える。**消える**（`AUDIOD_ENDPOINT_REMOVE`、または登録した接続が切れた）は状態ではなく削除で、route は既定に戻る（R4） |
| E5 | **開けてほしい**: route が DORMANT の endpoint を選んだ時、audiod は登録した daemon に `AUDIOD_ENDPOINT_WANTED {id, wanted=1}` を送る。daemon は開けて（headset の SCO を張る、A2DP を Start）ACTIVE にする。使う route が無くなれば `wanted=0`、daemon は自分の判断で閉じて DORMANT に戻す（例 A2DP は 5 秒の後に Suspend）。開けられなければ daemon が REMOVE するか DORMANT のまま `AUDIOD_ENDPOINT_FAILED {id, errno}`、audiod は route を既定に戻し `ROUTES` の event に理由を載せる。**これで「call の出力を headset にした時に誰が headset の SCO を開けるか」の鶏と卵を解く**（review-2 R8） |
| E6 | **寿命と検査**: 登録した接続が切れたら、その接続の endpoint は全部消える。daemon が書く位置は毎 period 検査する: 減る、または相手の位置との差が capacity を越える → その endpoint は壊れたとして消し log 1 行。状態が変わった時・XRUN の時は E5 の水位の制御（下の C3）を reset |

### R: route

| ID | 決め |
| --- | --- |
| R1 | **`media`**: app の再生の stream の mix → 出力の sink 1 つ（既定 `device:out`）。app の録音の stream ← 入力の source 1 つ（既定 `device:in`）。出力を headset-media（A2DP）にすると、app の音は全部そこへ（WS205 A1） |
| R2 | **`call`**: 通話の相手の側の group（phone-call か modem-call の sink と source）と、利用者の側の出力の sink と入力の source をつなぐ: 相手の source → 利用者の出力、利用者の入力 → 相手の sink。**張るのは相手の側の endpoint を持つ daemon**（bluetoothd が Synchronous Connection Complete の後に `AUDIOD_CALL_SET {far_group}`、終わりで `AUDIOD_CALL_CLEAR`、Q10 のとおり PC で応答・発信した通話だけ）。利用者の側の既定は下の R3 |
| R3 | **call の利用者の側の既定**: headset-voice の endpoint（ACTIVE か DORMANT）があればそれ、無ければ `device:out`・`device:in`。利用者は Phone の通話の card で「スピーカー／ヘッドセット」を選べる（Q14 の card。「スマホで話す」は route ではなく bluetoothd が SCO を切る操作で、ws197 B6 の `KL_PHONE_ASK_AUDIO`）。→ 判断 G1 |
| R4 | **既定へ戻す**: 出力・入力の endpoint が消えたら、その route は既定（R1・R3 の既定）へ。call の相手の側が消えたら call を外す。SUSPENDED では戻さない（review-2 R8: 通話の間 A2DP を Suspend しても media は headset のまま、speaker では鳴らない） |
| R5 | **通話の間の media（Q13）**: call が張られている間、**利用者の出力が `device:out`（speaker）なら media の stream を止める**: audiod は media の stream を読まない（app の ring は満ちて app は待つ: Music・動画は止まったのと同じ、call が外れると読み始める）、`AUDIOD_MEDIA_HELD {held}` の event を出す（subscribe した compositor が Music などに知らせる口は後）。**出力が headset なら media の mix を −12 dB で call の出力に重ねる**（headset の SCO の sink へ。A2DP は通話の間 Suspend） |
| R6 | **route を変える権限**: `AUDIOD_ROUTE_SET {route, output, input}` は peer の uid が root・`_bluetooth`・**seat の持ち主**（volumed と同じく `/dev/gpu0` の owner）の接続だけ。compositor は seat の持ち主の uid で動く。app は libkeiland の `kl_system_audio_*` で compositor に頼む。**「Phone・Settings だけ」は約束であって強制ではない**（app_id は自己申告、review-2 R4）。`CALL_SET`・`CALL_CLEAR` は相手の側の endpoint を登録した接続だけ |
| R7 | **知らせ**: `AUDIOD_SUBSCRIBE` の mask に `ROUTES` と `ENDPOINTS` を足す。`AUDIOD_ENDPOINT_INFO {id, kind, class, group, state, rate, name}`（1 endpoint に 1 message、一覧は `AUDIOD_ENDPOINT_LIST` の答えとして並べて最後に DONE）と `AUDIOD_ROUTE_INFO {route, output, input, active, reason}`（route ごと）。message は小さく保ち、一覧を 1 つの message に入れない |

### C: 拍と変換（review-2 R5・R6 の直し）

| ID | 決め |
| --- | --- |
| C1 | **master の clock は 1 つ**: `device:out` の period（HDA・Pi 4 の PWM）、device が無ければ今の timer。audiod は今と同じく 1 thread で、master の period ごとに全部の route を進める。外の endpoint の clock（daemon が ring を進める速さ: SCO・A2DP の送りの拍）は master と少しずれる |
| C2 | **外の endpoint ごとに ASRC 1 つ**（適応の rate 変換）: master の rate ↔ endpoint の rate。**多相の FIR**（Kaiser 窓の sinc、阻止域 −50 dB 以上、通過域の端は endpoint の rate の 0.425 倍: 8 kHz で 3.4 kHz、16 kHz で 6.8 kHz、44.1・48 kHz で 18.7・20.4 kHz。位相の表 256 と表の間の線形補間で任意の比）。比は公称の比 ×（1 + 補正）。stream（app）は今の線形補間のまま（この WS の範囲の外） |
| C3 | **水位の制御**: 各外の endpoint の ring の水位（sink: audiod が書いて daemon がまだ読んでいない frame、source: daemon が書いて audiod がまだ読んでいない frame）を period ごとに測り、目標（`target_frames`、SCO は 40 ms、A2DP は 60 ms）との差を PI で比の補正に（**±0.2% で頭打ち**、clock のずれ数百 ppm を吸う）。**大きなずれは比で追わない**: (a) 始まり: source は水位が目標に達するまで読まず無音、sink は目標まで無音を先に書く。(b) underrun（sink の daemon が空を読んだ、source が空）: 無音を入れて目標まで詰め直し、PI の積分を reset。(c) overrun（水位が目標の 2 倍を越えた）: 古い側を捨てて目標に戻し、PI を reset。詰め直し・捨ては period の境で、短い fade（2 ms）を掛けて click を抑える。数は endpoint ごとに数え、`AUDIOD_ENDPOINT_INFO` に載せる |
| C4 | **形式**: 外の endpoint は S16_LE、channels 1（voice）か 2（media）。mix は今の int64 の stereo のまま、mono の sink へは (L+R)/2、mono の source は両方の channel へ |
| C5 | **計算の量**: 8 kHz ↔ 48 kHz で約 180 tap（48 kHz の側）、1 方向 1.5 M 積和/秒。通話の 4 本（相手の受け・送り、headset の受け・送り）でも 6 M 積和/秒で、Pi 4 でも小さい |

### L: 遅れの予算（review-2 X4・S5・R7、向きごと）

| 段 | 相手 → 利用者の出力（ms、最大） | 利用者の入力 → 相手（ms、最大） |
| --- | --- | --- |
| SCO の packet の間隔 | 7.5 | 7.5 |
| USB の束ね（ws197-p007 X11: IN 8 packet、OUT 4 packet × 4 本） | 8 | 16 |
| kernel の SCO の ring と bluetoothd の SCO の thread | 2 | 2 |
| endpoint の ring の水位（C3 の目標） | 40 | 40 |
| ASRC の群遅延 | 2 | 2 |
| audiod の period（master） | 21.3 | 21.3 |
| HDA の先行（2 fragment） / HDA の録音の 1 period | 42.7 | 21.3 |
| 計 | **約 124** | **約 110** |

目標は向きごとに 150 ms 以下。headset の SCO が間に入る時（相手 → phone の SCO → audiod → headset の SCO）は ring・ASRC・SCO の段が 2 回になり約 190 ms（電話の普通の範囲。目標の例外として記録）。p003 の host 試験と ws197-p007c の実機で各段を測り、この表を直す。

### P: protocol の版 2

| ID | 決め |
| --- | --- |
| P1 | `AUDIOD_VERSION` 2。`HELLO` の version 1 の client（今の libkeiland・audio-loop など）は今の message だけ使える（互換）。新しい message は version 2 の HELLO の後だけ |
| P2 | `AUDIOD_MESSAGE_MAX` を 64 から 128 に（`ENDPOINT_ADD` は header 16 ＋ 本体 36 ＋ name 32 = 84 byte）。client の入力の buffer（`AUDIOD_CLIENT_INPUT` 1024）は変えない。libkeiland の backend の `STREAM_PENDING` はこの値から計算するので一緒に直す（review-2 の minor） |
| P3 | 新しい message: 要求 `ENDPOINT_ADD`(12)・`ENDPOINT_REMOVE`(13)・`ENDPOINT_STATE`(14)・`ENDPOINT_LIST`(15)・`ROUTE_SET`(16)・`ROUTE_LIST`(17)・`CALL_SET`(18)・`CALL_CLEAR`(19)・`ENDPOINT_FAILED`(20)。答えと event `ENDPOINT_ADDED`(73)・`ENDPOINT_INFO`(74)・`ROUTE_INFO`(75)・`ENDPOINT_WANTED`(76)・`MEDIA_HELD`(77)。SUBSCRIBE の mask に `ENDPOINTS`・`ROUTES` の bit |
| P4 | 試験の道具: `userland/tests/audio-endpoint`（偽の daemon: endpoint を登録し、指定の rate と clock のずれで ring を進め、正弦波を書く・読んで周波数と水位を記録）。host 試験と T1 の QEMU で使う |

### K: desktop の口と 3 OS

| ID | 決め |
| --- | --- |
| K1 | libkeiland: `kl_system_audio_endpoints_begin`・`kl_system_audio_route_set_begin` と結果の受け取り（ws157-p014 の media の API と同じ非同期の形: begin が request の番号を返し、`KL_SYSTEM_CHANGED_*` で知らせ、take で受ける）、route・endpoint の変化の event。compositor が backend を通して audiod に言う |
| K2 | `libkeiland-backend`: `kl_backend_audio_endpoints`・`kl_backend_audio_route_set`。zedBSD は audiod の protocol 2、**Linux・FreeBSD は ENOTSUP の stub**（`unsupported/audio-route-unsupported.c`）。backend の境界の checker と WS108・WS109 の build の file の表に足す（review-2 R4） |
| K3 | Settings の Sound: 「出力」の選び（media の route の出力: 内蔵／Bluetooth の headset）。WS205 の A1 が使う |
| K4 | Phone の通話の card: 「スピーカー／ヘッドセット」（call の利用者の側、R3）。「スマホで話す」は ws197-p007b1 の B6（`KL_PHONE_ASK_AUDIO`）で、この WS は card の上の置き場所だけを決める |

### T: 試験

| ID | 決め |
| --- | --- |
| T1 | host（audiod を host で build、P4 の偽の daemon と偽の device: 今の audiod の host の作りを使う）: 登録・権限（uid の偽装は host の試験の hook で）・状態（ACTIVE・SUSPENDED・DORMANT → WANTED）・削除と接続の切れで既定へ・位置の検査・route の切り替え・call の張り外し・Q13 の held と −12 dB |
| T2 | ASRC: FIR の周波数応答（8 kHz: 3.4 kHz まで ±0.5 dB、4 kHz 以上 −50 dB。16 kHz・44.1・48 kHz も同じ比で）、clock のずれ ±0.05% と ±0.1%（頭打ちの半分）で 10 分（模擬の時間）の水位が目標の ±10 ms に収まる、段の応答（目標から 20 ms ずらして 2 秒で ±5 ms に戻る）、underrun・overrun の後の詰め直し（click: 前後の sample の差が fade の範囲） |
| T3 | QEMU（T1）: image に audiod と audio-endpoint、`device:out` は QEMU の HDA。endpoint の登録 → route media を切り替えて正弦波が偽の daemon に届く、偽の daemon を kill して speaker に戻る、call の張り外し。合否は audio-endpoint の出力（SSH で読む） |
| T4 | 実機の通話・A2DP は ws197-p007c と WS205 p010（この WS では行わない） |

## Phase（案）

| Phase | 内容 | 見積 | 依存 |
| --- | --- | --- | --- |
| p001 | この設計と review | — | — |
| p002 | audiod: protocol 2、endpoint（E1〜E6）、route（R1〜R7）、権限、events、偽の daemon（P4）、T1 の host 試験 | 6 LW | p001 |
| p003 | ASRC と水位の制御（C1〜C5）、route の mix（call の 2 つの leg、Q13）、T2 の host 試験、遅れの表の測り直し | 5 LW | p002 |
| p004 | libkeiland・compositor・backend の口（K1・K2、3 OS と checker）、Settings の Sound の出力（K3）、Phone の card の選び（K4） | 4 LW | p002 |
| p005 | QEMU の試験（T3、T1 に依頼） | 1 LW | p003、p004 |
| p006 | 規約の全文の見直し | 1 LW | p005 |

計 約 17 LW（review-2 の見積もり 15〜20 の中）。

**使う側の開始の条件**: ws197-p007b1 は p002・p003 の main への統合の後（card の切り替えは p004 の後）。WS205 p004 は p002〜p004 の後。

## 判断（Q1 経由でユーザーへ、推し付き）

| ID | 問い | 推し |
| --- | --- | --- |
| G1 | 通話の時、headset が繋がっていれば最初から headset で話すか | **headset で**（携帯電話と同じ）。card で speaker に切り替えられる |
| G2 | A2DP の headset が繋がった時、media の出力を自動で headset へ移すか | **移す**（繋がった時に移り、切れたら speaker へ戻る）。Settings の Sound で手で戻せる |

## 未確認（p002 の最初に）

- audiod を host で build する今の作り（ws035・ws100 の host の試験があるか）。無ければ p002 で作る。
- `/dev/gpu0` の owner の規則が Pi 4（`bcm2711` の GPU の node）でも同じか。
