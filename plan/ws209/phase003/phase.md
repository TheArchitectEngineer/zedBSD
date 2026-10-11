<!-- awesome-plan project=zedbsd record=ws209-p003 -->

# ws209-p003: ASRC と拍、外の endpoint の音、call の脚

Parent: [WS209](../ws.md)
Status: in-progress（P1。M8・M9 を決めた、ASRC と host 試験は済み、audiod への統合が次）
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

## 次（この Phase の中）

audiod への統合: 外の endpoint の ring の読み書き（E6 の位置の検査と SIGBUS、N16 の書き手）、media の出力が外の sink の時に mix を ASRC で endpoint の rate へ、録音の入力が外の source の時、call の 2 つの脚（相手の source → 利用者の出力、利用者の入力 → 相手の sink）、Q13（speaker の時は media を止め alert は −12 dB、headset の時は −12 dB で重ねる）、SUSPENDED・DORMANT の間は書かない（N20）、`played_position`（N4）、外の sink の soft volume（N5）、M9 の中間の段、call の間の先行 1 fragment（M8・G4）。試験は host の audiod と偽の daemon（T2 の続き）。
