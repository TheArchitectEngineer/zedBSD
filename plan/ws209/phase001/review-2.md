# ws209-p001 設計 1.1（E・R・P）の再 review（2026-10-11 design-reviewer、Q1 が要約して写した）

総評: p002 にはまだ入らない。M1・M2 は解け、M4・M7 はほぼ、M3・M5・M6・M11 は一部。**設計 1.2 で N1〜N5 を直してから p002**（数行の決め、版は不要、Q1 の確認で足りる）。N6 は p004 の範囲と依存（WS191）。N7 は G5 としてユーザーへ。minor は p002 の中で。HAL・kernel の UAPI・`AUDIOD_SHM_VERSION`（1）は不変。

| # | 重さ | 箇所 | 問題 | 推しの直し |
| --- | --- | --- | --- | --- |
| N1 | major | E5''（期限切れ） | A2DP の Start が 2 秒を越えると route を `device:out` へ戻し曲を内蔵 speaker で鳴らす。R4（SUSPENDED では戻さない）と食い違い、bluetoothd の main の 1〜2 秒の停止で普通に起きる | 期限切れで route を変えない。`ROUTE_INFO.reason=TIMEOUT`、daemon に `WANTED=0`。待たせた stream は「捨てながら進める」か「待たせ続ける」を決める。speaker へ移すかは compositor（R8'）。T1 に「期限切れでも speaker から音が出ない」 |
| N2 | major | E5'・E5''・E4（WANTED の競合） | `WANTED=0` の後、daemon が Suspend 中でもまだ ACTIVE の間に新しい stream で `WANTED=1` → audiod は書き続け ring が満ちて音の頭が消える | `WANTED=0` を送った時点で「開いていない」とみなして書かずに待たせる。`WANTED` に通し番号、daemon がその番号付きの `ENDPOINT_STATE ACTIVE` を返すまで書かない。SUSPENDED→ACTIVE も同じ |
| N3 | major | E7 | 「SUSPENDED → ACTIVE で audiod が sink の残りを無音で埋め直す」は「flush は位置の持ち主だけ」と矛盾し競合する | 古い中身は daemon が ACTIVE を送る前に read_position を write_position まで進めて捨てる。audiod は SUSPENDED の間は書かず、ACTIVE を受けたら C3 (a) の「目標まで無音を先に書く」から |
| N4 | major | E9、`mix.c:293`、`audio.c:449-482`、`videoplayer/audio.c:191` | latency を played_time に足しても、唯一の使い手 `vp_audio_clock_position` は `kl_audio_stream_position(…, NULL)` で時刻を捨て位置だけ使う → M11 は解けていない。時刻が未来になり `device:out` の先行（`device.c:35・438`、kernel の lead）も入らない | 時刻は今のまま、`played_position` を「今聞こえている位置」に（read_position から今の出力の遅れ（`device:out` の先行と lead、外の sink は水位 + `latency_us`）を引く）。出力が変わって戻りそうなら止めて待つ（単調）。ring の layout は不変。T2 に「headset の遅れで position が遅れる」 |
| N5 | major | R7'（音量） | (a) `DEVICE_VOLUME` は常に media の出力に効き、通話中に通話の音量を変えられない (b) headset の釦の音量（AVRCP・`+VGS`）を daemon→audiod に戻す message が無い (c) daemon が音量に対応するかが分からない (d) 出力の変更の直後の音量が未定で大きな音が出うる | call の間は `DEVICE_VOLUME`・`VOLUME_CHANGED` を call の利用者の出力へ。ENDPOINT_ADD に `flags`、daemon→audiod の `ENDPOINT_VOLUME_REPORT`。endpoint ごとの音量、初めは安全な値（50% か daemon の報告）。0..100 と AVRCP 0..127・`+VGS` 0..15 の変換は daemon |
| N6 | major | R5'、Phase 表 | app の stream の役を運ぶ経路（`kl-audio-protocol.h`・compositor の `audio-stream.c`・backend `audio-stream-zedbsd.c:593-603`）がどの Phase にも無い → 全 app が media になり、speaker の通話で通知音・2 本目の着信音・Phone の音も止まる | p004 に「stream の役を kl-audio の wire・compositor・backend へ（版と古い compositor・app の扱い）」、依存に WS191、p003 の T2 は直接の client、p004 の試験に「app の alert が通話中に鳴る」 |
| N7 | major（判断） | R4 と R8' | headset が切れた時・bluetoothd の再起動の間、audiod が media を speaker へ自動で戻し曲を大きな音で流す（携帯電話は audio becoming noisy で止める）、R8' とも食い違う | 利用者の操作によらず出力が消えたら `MEDIA_HELD reason=OUTPUT_LOST` で待たせ、compositor が ROUTE_SET を出すまで鳴らさない。**G5** をユーザーへ（推し: 止める） |
| N8 | minor | E5' | 録音の stream が「使う」に入らず DORMANT の headset-voice の source を開けない。`AUDIOD_FEEDBACK`（main.c:431）は WANTED を出さない。猶予が daemon 5 秒 + audiod 5 秒 | 録音の stream も「使う」に、FEEDBACK を WANTED の契機にするか鳴らないと書く、猶予は audiod の 5 秒だけ |
| N9 | minor | P1' | 3 つの HELLO（`audio-zedbsd.c:339`・`audio-stream-zedbsd.c:439`・`audio-loop/main.c:375`）が `AUDIOD_VERSION` を protocol の版として送る | p002 で `AUDIOD_PROTOCOL_VERSION_1` に、assert を `AUDIOD_SHM_VERSION` に、`AUDIOD_VERSION` は消す。version 0・短い HELLO（main.c:292）は 1 |
| N10 | minor | P1''' | audiod は SUBSCRIBE の mask を bit で見ない（main.c:444-447・660）。MEDIA_HELD・ENDPOINT_REMOVED の bit が未定 | `mask & VOLUME` で判定、版 1 は VOLUME の bit だけ。MEDIA_HELD は ROUTES の bit |
| N11 | minor | R5'、main.c:307 | STREAM_CREATE は長さがちょうどでないと EINVAL、`role` を足すと版 1（40 byte）が壊れる | 版 1 の struct は不変、`audiod_stream_create2`（48 byte）を版 2 だけ、版 1 の長さなら role=media |
| N12 | minor | R5'' | CALL_STATE の送り手・番号（P3 に無い）・効き目が未定 | far の endpoint を登録した接続だけ、P3 に番号、G3（止めない）なら audiod は記録だけ |
| N13 | minor | R6'、main.c:198 | 版 1 の録音は 0666 で uid を問わず開ける → speaker の通話の声をどの local の uid でも録れる | 通話中の録音を seat の uid と root に限るか、残る穴として明記 |
| N14 | minor | R6' | far は phone-call・modem-call の group だけ、の逆の検査が無い。id の使い回し | CALL_SET の far_group の class を検査、id は使い回さない 32 bit |
| N15 | minor | E10 | `bt:…:voice` は sink と source で同じ key、重複・内の endpoint の key が未定 | 一意は (key, kind)、重複は古い方を消すか EEXIST、内は `dev:dsp0:out` など |
| N16 | minor | E7、protocol.h:173-175 | endpoint の ring の underruns・overruns・played_*・state の書き手 | sink は underruns を daemon・overruns を audiod、source は逆、played_*・state は audiod か使わない、daemon の数は表示だけ |
| N17 | minor | E8 | audiod の再起動で ROUTE_SET が消える、REMOVED の後の再登録・CALL_SET | compositor は WELCOME の後に選びを送り直す、daemon は REMOVED で登録し直し通話中なら CALL_SET |
| N18 | minor | P2 | ENDPOINT_ADD・INFO が約 120 byte、flags で 128 に届く、P3 の番号表が古い | 数え直し `AUDIOD_MESSAGE_MAX` 256 か余裕を明記、P3 に番号 |
| N19 | minor | P2' | WELCOME の `device=出力がある` を版 2 だけにすると、困る版 1 の backend（audio-stream-zedbsd.c:587）が p004 まで直らない | 意味を全部の版で変える |
| N20 | minor | E4 | DORMANT は待たせるのに SUSPENDED は捨てる、マルチポイントで曲が無音で進む | SUSPENDED も待たせる（通話の R5' を除く）か理由を書く |
| N21 | minor（推測） | R3・K4 | speaker と内蔵の mic で話す時の AEC が無い、AG の EC/NR（NREC）に頼れるか未確認 | 前提か範囲外かを明記、ws197-p007c で確かめる |
| N22 | minor | 全体と ws.md | p002 の範囲が増えたのに 6 LW、main の ws.md:24 の権限の書き方が古い | p002 を 7 LW、ws.md は Q1 が揃える |
| N23 | minor | main.c:36・214 | 接続の上限 64 を誰でも埋められ bluetoothd が登録できない | root・`_bluetooth` の枠を残すか残る穴として明記 |
