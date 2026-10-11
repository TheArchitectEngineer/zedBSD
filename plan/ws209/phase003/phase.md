<!-- awesome-plan project=zedbsd record=ws209-p003 -->

# ws209-p003: ASRC と拍、外の endpoint の音、call の脚

Parent: [WS209](../ws.md)
Status: in-progress（P1。M8・M9、ASRC、audiod への統合と host 試験は済み（未統合）。T2 の残りと遅れの表の測り直しは実機（ws197-p007c）、QEMU は p005）
Phase disposition: normal
Queue: P1 の lane

## 先に決めた M8・M9（設計 1.1 の行き先、ユーザーの G4）

| ID | 決め |
| --- | --- |
| M8 | 遅れの表に kernel の mmap の lead（2 fragment、`audio.c:67`）と kernel の SCO の送りの queue（ws197-p007a2 の 12 packet ≒ 36 ms、ただし水位で埋まるのは 2〜3 packet）を足す。**G4（ユーザー）: call の間だけ詰める**: SCO の endpoint の水位の目標を 20 ms（target_frames、bluetoothd が登録で渡す、8 kHz で 160 frame）、call の間は audiod の device の先行を 1 fragment（`AUDIOD_FILL_FRAGMENTS` を call の時だけ 1）。目標の片道 150 ms、届かなければ約 170 ms を受け入れる。各段の測り方: 相手 → speaker は bluetoothd の SCO の受けの時刻と audiod の `played_position`（N4 の今聞こえている位置）の差、利用者 → 相手は audiod の録音の時刻と bluetoothd の送りの時刻の差、どちらも 5330 の実機で p007c に |
| M9 | 水位の制御を 2 段に: **誤差が 10 ms 以内は PI**（`asrc_steer`、±0.2% で頭打ち）、**10 ms を越えたら中間の段**: 2 ms の fade を掛けて、sink は書く量を（誤差 − 目標の差）だけ減らすか無音を足し、source は読み捨てるか無音を足し、PI の積分を 0 に。測る点: 各 period の境の水位（sink は write − read、source は write − read）を 8 period の移動平均で（鋸歯と A2DP の 20 ms の拍との beat を均す）。T2 の基準: ±0.05%・±0.1% のずれで 10 分、最初の 10 秒の後 ±10 ms 以内、8 ms の段を 10 秒で ±2 ms 以内（PI だけの範囲）。10 ms を越える段は中間の段の試験で |

## ASRC（2026-10-11 P1）

`userland/base/audiod/asrc.c`・`asrc.h`（純粋、audiod に依らない）: 多相の FIR（Kaiser の窓の sinc、阻止域 60 dB の見積もり、通過域は低い方の rate の 0.425 まで、阻止域は 0.5 から、256 位相と位相の間の線形補間、taps は 8 kHz ↔ 48 kHz の下りで 290 程度・上りで 50、最大 512）、PI（比例 0.0005・積分 0.00002、補正は ±0.2%）、XRUN の後の `asrc_reset`。

`sh plan/ws209/tests/run-asrc-test.sh`（通常と ASan/UBSan）: all PASS。測った値: 8 kHz の下り pass −0.00 dB・端 −0.01 dB・阻止 −66.3 dB、上り 0.00 dB、16 kHz の下りの阻止 −66.5 dB、44.1 kHz の下りの阻止 −65.1 dB、8 kHz → 48 kHz の出力の数は比のとおり、ずれ +0.05% で 10 分の最悪 3.12 ms・−0.1% で 6.18 ms、8 ms の段は 10 秒で 0.00 ms。

## audiod への統合（2026-10-12 P1）

`userland/base/audiod/route.c` の末尾（flow）、`mix.c`・`device.c`・`audiod.h`・`protocol.h`・`Makefile`（asrc.c を足した）。

| 項目 | 実装 |
| --- | --- |
| stream を読むか | `audiod_route_stream_plays`: 出力が開いている（device の物、または daemon が ticket 付きの ACTIVE を返した物、N2・N20）。call が流れている間（set かつ `audio_here`、G3）は call の出力: speaker なら media を読まず alert は読む、headset なら両方（Q13） |
| Q13 の −12 dB | `audiod_route_stream_gain`: call が流れている間は stream の音量に 0.25 を掛ける |
| speaker の call の間の hold | `MEDIA_HELD {held=1, reason=CALL}`（`AUDIOD_REASON_CALL` = 6 を足した）、call が外れるか headset へ移ると `held=0`（出力を失った hold は残す） |
| 外の sink | `audiod_flow_playback`（feedback の後・device の soft volume の前）: media の出力が daemon の sink なら mix を float にし endpoint の音量（daemon が音量を持つ `FLAG_VOLUME` なら 1、それ以外は device と同じ曲線、N5）で ASRC に通し S16 で ring へ、device には無音 |
| call の脚 | 相手の source を ASRC で読み、speaker なら device の mix に足す、headset なら mix（−12 dB 済み）を重ねて headset の voice の sink へ。利用者の入力が daemon の source なら読んで相手の sink へ、device の mic なら録音の period ごとに相手の sink へ（`audiod_flow_capture`、call の間は device の録音を始める）。相手の sink には利用者の音量を掛けない |
| media の入力が daemon の source | `audiod_flow_input`: 録音の stream に device の period の代わりに渡す（device の録音を拍に）。call が同じ source を読んでいる間は無音（ring の読み手は 1 つ） |
| 水位（C3・M9） | 各 period の境の水位（sink は書く前の write − read、source は読んだ後に残る量）の 8 period の平均。窓が埋まる前と 10 ms 以内は PI（`asrc_steer`）、窓が埋まって 10 ms を越えたら中間の段: sink は書く量を減らすか無音を足す（書いた端を 2 ms で fade out、次を 2 ms で fade in）、source は古い block を読み捨てて 2 ms の fade in、積分を 0、窓を空に |
| 初めの充填と XRUN | sink は開いて初めの書きで目標まで無音を先に書く（N3・C3 (a)）。source は「目標 + 1 period」溜まってから読み、空になったら無音・converter を初めから・充填し直し |
| 位置の検査（E6） | 位置が逆・容量を越えたら、また ring の memory の SIGBUS で endpoint を `REMOVED reason=BROKEN`（`route_remove`）。SIGBUS の受けは sigsetjmp を単独の式で呼び guard の値で戻りを見分ける（規約の call-in-condition に合わせた） |
| 数（N16） | sink の overruns（ring が満ちて捨てた）と source の underruns（空）は audiod が数える |
| SUSPENDED・DORMANT | 開いていない endpoint には書かず読まない。ENDPOINT_STATE を受けたら converter・充填・平均を初めから |
| M8・G4 | `audiod_route_fill_fragments()`: call が流れている間は device の先行を 1 fragment（他は 2）、`device.c` の `AUDIOD_FILL_FRAGMENTS` を置き換えた |
| played_position（N4） | read_position から今の出力の遅れ（device: 先行 fragment + kernel の lead、daemon の sink: 水位 + `latency_us`、call の間は call の出力）を引き、単調に（`stream->played_last`）。時刻は今のまま |

## host 試験（T2 の host の分）

`sh plan/ws209/tests/run-audiod-route-test.sh`（host の audiod を ASan/UBSan で、device の無い timer の拍）: 第 3 の audiod に `audiod-route-test SOCKET flow`、この process が daemon の側の ring を実時間で読み書きする。2026-10-12: `audiod-route: all PASS` ×2、`audiod-route: flow all PASS`。

| 確かめた事 | 測った値 |
| --- | --- |
| media → headset の 44.1 kHz の sink（`FLAG_VOLUME`、目標 40 ms = 1764 frame） | 1 kHz 0.5 の tone が 0.4999、1.5 kHz 0.0000、水位 1800〜2867 frame（目標 − 10 ms 〜 目標 + 1 period + 10 ms の中）、空読み 0 |
| played_position（latency 50 ms） | read から 4497 frame（48 kHz）遅れ（50 ms + 水位） |
| SUSPENDED | 書かない、stream は待つ、WANTED の ticket の ACTIVE で再開して tone が戻る |
| media の入力 = headset の 16 kHz の voice の source | 録音の stream に 500 Hz 0.3 が 0.3000 |
| call（相手 8 kHz・目標 20 ms、headset の voice 16 kHz） | headset に相手の 400 Hz 0.4 が 0.3999、media 1 kHz 0.5 が 0.1250（−12 dB）、相手の sink に利用者の 700 Hz 0.3 が 0.2998、両方の sink の空読み 0 |

build: `make BUILD=build/ws157-q937 ZEDBSD_CONFIG=config/current-uat.mk build/ws157-q937/bin/audiod`（warning 0）。`python3 plan/tools/style-check.py` は route.c・asrc.c・試験で 0、mix.c・device.c・protocol.h は前からの指摘だけ（変えた行は 0）。

途中で直した事: 平均の窓を空にする時に書く位置を戻していなかった（古い値が平均に残り段が振れた）、窓が埋まる前に中間の段が動いた、source の目標が 1 period より小さいと毎回空になった（「目標 + 1 period」で充填）、相手の sink に利用者の音量が掛かった。

## 未実施・残り

- host で試せない事: device:out・device:in（host に /dev/dsp0 が無い）の speaker の call（相手の声を device の mix へ、media を止める、`MEDIA_HELD reason=CALL`）、device の mic → 相手の sink、call の間の先行 1 fragment。p005（QEMU）か 5330 の実機で。
- 10 分の ±0.05%・±0.1% のずれの試験は ASRC 単体（`run-asrc-test.sh`）で済み。audiod を通した長時間は p005・実機で。
- 遅れの表の測り直し（M8 の各段）は 5330 の実機で（ws197-p007c）。
- converter の半窓（1 ms 未満）は played_position に入れていない。
