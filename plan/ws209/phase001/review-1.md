# ws209-p001 設計 第 1 版の敵対的 review（2026-10-11 design-reviewer、Q1 が要約して写した）

## 総評

p002 の実装にはまだ入らない。review-2 の R の指摘のうち拍（C1〜C3）・一時停止と消えたの区別（E4）・鶏と卵（E5）・3 OS の stub（K2）・Q13 の推し（R5）・見積もり（p006）・MESSAGE_MAX（P2）は方向として解けた。p002 の前に設計 1.1 で M1〜M7・M11 を直し E・R・P 節の短い再 review。p003 の前に M8・M9、p004 の前に M10。ユーザーに G3・G4 を足して聞く。HAL・kernel の UAPI は不変（audiod の protocol は userland）。

## 指摘

| # | 重さ | 箇所 | 問題 | 推しの直し |
| --- | --- | --- | --- | --- |
| M1 | major | P1（版 2） | `AUDIOD_VERSION` は shm の ring の版も兼ねる（audiod/main.c:518、backend の `_Static_assert` audio-stream-zedbsd.c:73、app は `version != 1` で EPROTO audio.c:634）。2 に上げると build が止まり、assert を外すと全 app の音が止まる。audiod は client の版を覚えない（main.c:297） | protocol の版（`AUDIOD_PROTOCOL_VERSION` 2）と ring の版（`AUDIOD_SHM_VERSION` 1、不変）を分ける。client に交渉した版、WELCOME は min。SUBSCRIBE の mask の bit を protocol.h に（今は backend の `AUDIO_SUBSCRIBE_VOLUME 1` だけ、audio-zedbsd.c:48）。v1 には新しい event を出さない |
| M2 | major | R6（権限）と route の枠 | 枠ごとの class の制約が無い。socket 0666（main.c:198）、seat の持ち主の uid は全 app の uid → app が ROUTE_SET で通話の相手の声を録る・通話へ流し込める。ws.md と phase.md の権限の書き方が違う | 枠ごとに許す class の表を audiod が強制（media 出力: speaker・headset-media（・headset-voice）、media 入力: mic・headset-voice の source、call の利用者側: speaker・mic・headset-voice、phone-call・modem-call は CALL_SET の far だけ）、違反は EINVAL。uid だけでは compositor と app を区別できないと明記、ws.md を揃える |
| M3 | major | E4・E5（DORMANT と WANTED） | (a) WANTED の答えに期限が無い (b) A2DP の Start・SCO の確立を待つ間に通知音・曲の頭が消える (c)「使う」の定義が無く A2DP が止まらず電池を使うか、音のたびに Start を待つ | 「使う」= media は running な再生の stream がある時（idle の猶予 5 秒は audiod）、call は CALL_SET の間。開ける間は media の stream を読まずに待たせ、期限（A2DP 2 秒・SCO 3 秒）で既定へ戻し `ROUTE_INFO.reason=TIMEOUT`。T1 に黙る・遅い・FAILED の daemon |
| M4 | major | E2・E6・C3（daemon の側の契約） | sink の overrun で古い側を捨てられない（read_position は daemon）、sink の空を audiod が知れない、underruns・overruns の書き手、source が capacity を越えると消される、audiod 側の削除の通知が無い、audiod の再起動の再登録・CALL_SET、SUSPENDED→ACTIVE の古い中身 | 「daemon の契約」の節: sink は write_position まで読み空なら無音で underruns++、source は capacity まで書き満ちたら新しい方を捨てて overruns++、audiod 側の sink の overrun は書かない・source の underrun は無音、`ENDPOINT_REMOVED{id, reason}`、audiod が切れたら間隔を置いて再登録と CALL_SET、flush は各位置の持ち主 |
| M5 | major | R5（Q13 の MEDIA_HELD） | (a) 通知音・Phone の着信音・compositor の音も止まる、DRAIN の DRAINED が来ない（mix.c）、通話の後に古い通知がまとめて鳴る (b) スマホで応答・スマホへ移した通話では route が無いので音楽が戻る (c) 動画が固まる、知らせる口が無い | stream に役（media・alert、STREAM_CREATE の v2 の field と libkeiland の口）、alert は止めずに −12 dB か捨てる。bluetoothd から call の状態を audiod へ。**G3**「通話の音がスマホにある間も media を止めるか」をユーザーへ。通知の口は Future Work か Phase |
| M6 | major | 音量 | DEVICE_VOLUME は /dev/mixer0 か device の soft_volume だけ（device.c、mix.c:323-324）→ A2DP・headset の SCO では volume の key と slider が効かず全音量 | 「音量は今の出力に効く」。外の sink には audiod の soft volume か daemon へ（AVRCP absolute volume・+VGS）。VOLUME_CHANGED は今の出力の値。feedback は media の出力へ |
| M7 | major | R3・R4・G2（policy の持ち主と保存） | G2 の自動の移動・既定・手で戻した選びの持ち主が無い。endpoint は登録ごとの id と name だけで、再接続・bluetoothd の再起動をまたいで覚えられない。desktop.conf を書くのは compositor だけ（Guardrail） | audiod は仕組みと固定の既定への戻しだけ、policy（G2・覚えた選び）は compositor。ENDPOINT_ADD・INFO に安定の `key`（例 `bt:<address>:a2dp`）。name は UTF-8 の境で切る |
| M11 | major | E2（endpoint の遅れ） | A2DP の Delay Report・SCO の遅れの field が無く、played_time（mix.c `audiod_played_store`）が数百 ms ずれる。後で足すと版をもう一度上げる | `latency_us` を ENDPOINT_ADD・STATE に今入れ、route の stream の played_time に足す |
| M8 | major | L（遅れの表） | kernel の mmap の lead 2 fragment（audio.c:67・2090・2258-2260）が抜け、audiod の先行 2 fragment（device.c:35・438）と足すと HDA の出力は 64〜85 ms、相手→speaker は約 167 ms で 150 ms を越える。利用者→相手の最大に kernel の SCO の送りの queue（約 40 ms）が無い | 表を足し直す。候補: SCO の水位の目標を 20〜25 ms、call の間は audiod の先行を 1 fragment、または約 170 ms を受け入れる（**G4**）。p003 で各段の測り方を決める |
| M9 | major | T2 と C3 の矛盾 | 「20 ms ずらして 2 秒で ±5 ms」は ±0.2% の PI では届かない（2 秒で最大 4 ms、少なくとも 7.5 秒）。水位を鋸歯のどこで測るか、A2DP の 20 ms との beat（約 320 ms） | 中間の段（誤差 10 ms 超は fade 付きで block を足す・捨てる）か基準を 10 秒以上に。測る点と filter を決める |
| M10 | major | K・T（desktop の口の検証） | `kl_system_audio_v1` は opcode 0〜2（kl-system-protocol.h:873-878）、新しい request・event・版・古い compositor の動作が無い。backend v2 の host 試験・Linux/FreeBSD の build・Settings/Phone の UI の試験が無い。T3 は compositor を通らない | K1 に wire（`KL_SYSTEM_SINCE_*`、古い compositor は ENOTSUP）。偽の audiod の backend の host 試験、`keiland-linux.mk`・FreeBSD の build と `plan/tools/keiland-os-boundary/check.sh`、QEMU の AAT（偽の headset、Settings で出力を選ぶと ROUTE_INFO が変わる）、Phone の card は UI 試験か UAT |
| — | minor | C5・T2（FIR） | 3.4→4.0 kHz を −50 dB は約 234 tap、「約 180」では T2 を満たさない。阻止域 4.6 kHz なら約 117 tap | T2 の阻止域と tap 数を揃える |
| — | minor | C1（録音の脚） | device:in は `read_capture` で別に進む、call が device:in を使う時の `audiod_device_start_capture` が無い、Pi 4 は device:in が無い | 録音の脚は `audiod_capture_period` で、call の route で録音を始める、Pi 4 は headset を勧める表示 |
| — | minor | 遅い client の切り捨て | `send_message` は MSG_DONTWAIT で送れないと client を落とす（main.c:629-630）、bluetoothd の main は 1〜2 秒止まりうる | endpoint の接続には REQUEST・UNDERRUN の event を出さない、数を抑えるか貯める |
| — | minor | E6（SIGBUS） | daemon が ftruncate できる、ASRC の loop を sigsetjmp（mix.c:216）の下に、どちらの ring か分ける | 明記 |
| — | minor | T1（uid の偽装） | 試験の hook が実行時だと本番に穴 | compile 時の host の build だけ |
| — | minor | Phase と試験 | p002 の T1 が p003 の実装（Q13・call の脚）を試す | T2・p003 へ移すか R5 を p002 に |
| — | minor | 依存 | WS191（incomplete）が `audio-stream-zedbsd.c` を変えている | 依存の表に WS191 の順 |
| — | minor | WELCOME | `device=0` だと backend の stream が NO_DEVICE（audio-stream-zedbsd.c:585-588）、HDA 無しで A2DP だけの機械で鳴らない | v2 で `device` を「出力がある」に |
| — | minor | E2・R2 の検査 | buffer_frames ≤ `AUDIOD_BUFFER_MAX`・target < capacity/2、ENDPOINT_STATE・REMOVE・FAILED は登録した接続だけ、call が張られた時の CALL_SET は EBUSY、in-band の着信音の扱い | 表に足す |
| — | minor | 見積もり | p004 の 4 LW は少ない | p004 5〜6 LW、計 18〜19 LW |
