<!-- awesome-plan project=zedbsd record=ws204-p003 -->

# ws204-p003: audio-loop（録音の確かめの道具）

Parent: [WS204](../ws.md)
Status: in-progress（q929-i01、P1。実装・host 試験済み、QEMU を T1 へ）
Phase disposition: normal
Queue: q929

## 設計（p001 R5）

`userland/tests/audio-loop/`（test の image だけ）: audiod の 1 つの接続に capture（stream 1、2ch S16、1 秒の ring）と playback（stream 2）を開き、1 秒の無音を録ってから 1 kHz・-12 dBFS の正弦波を 3 秒鳴らしつつ録り続ける。無音（0.2 s〜）の RMS と 1 kHz の Goertzel（Hann）、音（開始の 0.4 s 後〜）の Goertzel と RMS、peak を出し、音 − 雑音の RMS ≥ `--margin`（20 dB）で PASS、全部 0 なら SILENT。`--record=S --wav=PATH` は録るだけ、`--device-volume=N` は出力の音量。1 行 `AUDIOLOOP ... result=`。

## 進み

- bf6768ead: `main.c`・`measure.c/.h`・`Makefile`。
- 続き: option の読みを表に（規約）、`plan/ws204/tests/audio-loop-host-test.c`・`run-audio-loop-host-test.sh`（rm 無し、build/tmp）、試験の image の `plan/ws204/tests/config-amd64-audio.mk`（current-uat ＋ audio-loop）。

## 確認

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws204/tests/run-audio-loop-host-test.sh` | plain・ASan/UBSan とも all PASS（-20 dBFS の正弦波が tone・RMS とも -20.0、DC や 997.3 Hz でも -20.0、-60 dBFS の雑音の RMS -60.0、その 1 kHz の bin -97、雑音の中の音 -20.0、無音は floor、2 回に分けた音 = 1 回、WAV の header の bytes） |
| `make BUILD=build/ws157-q937 ZEDBSD_CONFIG=config/current-uat.mk … bin/audio-loop` | rc 0、warning 0（host の cc でも -Werror で通る） |
| `plan/tools/style-check.py` main.c・measure.c | 0 |

## T1 への依頼（QEMU）

image = `plan/ws204/tests/config-amd64-audio.mk`、QEMU に `-device intel-hda -device hda-duplex`（audiodev none）。SSH で (1) `audio-loop` → `AUDIOLOOP rate=48000 frames=216000 expected=216000 ms≈4500 overruns=0 underruns=0 … result=SILENT`（none の audiodev は無音、rc 1 が期待）、(2) `audio-loop --record=2 --wav=/tmp/r.wav` → `AUDIOLOOP record rate=48000 frames=96000 ms≈2000 overruns=0`、`/tmp/r.wav` は 384044 bytes、(3) dmesg に `hda: input pin` の行と `hda: capture from pin 0x.. (connected pin)`。合格: (1) の frames が expected と同じ・ms が 4000〜6000・overruns 0、(2)・(3) のとおり。

未実施: QEMU（T1）、5330（p004）。
