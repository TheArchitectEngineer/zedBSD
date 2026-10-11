<!-- awesome-plan project=zedbsd record=ws204-p003 -->

# ws204-p003: audio-loop（録音の確かめの道具）

Parent: [WS204](../ws.md)
Status: in-progress（q929-i01、P1。q937 の review のため中断）
Phase disposition: normal
Queue: q929

## 設計（p001 R5）

`userland/tests/audio-loop/`（test の image だけ）: audiod の 1 つの接続に capture（stream 1、2ch S16、1 秒の ring）と playback（stream 2）を開き、1 秒の無音を録ってから 1 kHz・-12 dBFS の正弦波を 3 秒鳴らしつつ録り続ける。無音（0.2 s〜）の RMS と 1 kHz の Goertzel（Hann）、音（開始の 0.4 s 後〜）の Goertzel と RMS、peak を出し、音 − 雑音の RMS ≥ `--margin`（20 dB）で PASS、全部 0 なら SILENT。`--record=S --wav=PATH` は録るだけ、`--device-volume=N` は出力の音量。1 行 `AUDIOLOOP ... result=`。

## 進み

- bf6768ead: `main.c`・`measure.c/.h`・`Makefile`（host の cc で -Wall -Wextra -Werror 通る、usage rc 2、audiod 無しで rc 3）。
- 再開: `plan/ws204/tests/audio-loop-host-test.c`（measure の試験: -20 dBFS の正弦波と -60 dBFS の雑音の測り、無音、WAV の header）、zedBSD の build（`build/<dir>/bin/audio-loop`）、T1 への依頼（QEMU hda-duplex: frames ≈ rate×4.5、overruns 0、result=SILENT が期待）。
