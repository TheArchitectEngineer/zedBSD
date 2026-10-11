<!-- awesome-plan project=zedbsd record=ws207-p001 -->

# ws207-p001: Raspberry Pi 4 の音（再生と録音）の調査と設計

Parent: [WS207](../ws.md)
Status: in-progress（q933、P1。設計の第 1 版、判断の点を Q1 へ）
Phase disposition: normal
Queue: q933（[P1 の lane](../../agents/P1/queue.md)）

## 由来

2026-10-11 ユーザー:「RPi4のオーディオを実装。（再生、録音）」

## 今の tree と実機の事実（2026-10-11、Pi 4 は読むだけ）

| 所 | 事実 |
| --- | --- |
| 音の枠 | `src/drivers/audio/audio.c`（`/dev/dsp0`・`/dev/mixer0`、再生と録音の ring、HDA の backend）、audiod。Pi 4 の backend は無い |
| Pi 4 の音の出口 | (1) **3.5 mm の jack**: PWM1（`pwm@7e20c800`、ARM 0xfe20c800、clock 0x1e を 50 MHz に）の 2 channel、GPIO 40・41 の ALT0（DT の `audio_pins`）、DMA で PWM の FIFO へ。(2) **HDMI の音**: HDMI の controller（`hdmi@7ef00700`・`7ef05700`、`audio` の clock、MAI の FIFO、DMA）。今の表示は firmware の KMS（`firmwarekms`）で HDMI は VideoCore の firmware が持つので、ARM から HDMI の音の packet を設定すると衝突する。(3) **VideoCore の音の service**（VCHIQ の "AUDS"、Linux の `snd_bcm2835`）: firmware が jack にも HDMI にも鳴らす。DT の overlay の `audio` の引数がそれ。tree に VCHIQ は無い |
| 録音 | Pi 4 に mic は無い。**USB の audio class（UAC1）の mic・headset** が要る → xHCI（VL805 も xHCI）の isochronous（ws197-p007a1）と UAC の driver が要る |
| DMA | tree に BCM2711 の DMA controller の汎用の driver は無い（ethernet・PCIe は自前の DMA） |

## 設計（案）

| ID | 決め | 理由 |
| --- | --- | --- |
| P1 | **再生は 3.5 mm の PWM から**: BCM2711 の DMA controller の小さな driver（channel 1 本、control block の輪、`src/drivers/platform/rpi4/rpi4-dma.c`）、clock manager（PWM の clock、mailbox か CM の register）、PWM1 の 2 channel を FIFO＋DMA で、48 kHz・16 bit を PWM の幅（約 10〜11 bit 相当、range = clock / rate）に変換。audio.c の backend として | ARM だけで閉じる、firmware の KMS と衝突しない。音質は PWM（11 bit 程度）だが確かめが易しい |
| P2 | HDMI の音は **VCHIQ の音の service** を後の Phase に（firmware が HDMI へ鳴らす）。ARM の HDMI の MAI 直は firmware の KMS と衝突するので使わない | 表示の作りを変えない |
| P3 | **録音は USB の audio class（UAC1）**: ws197-p007a1 の xHCI の isochronous の上に、UAC1 の driver（AudioControl・AudioStreaming の interface、PCM 16 bit、alternate、sample rate の control、IN の isoch）を audio.c の録音の backend に。Pi 4 だけでなく 5330 にも効く | Pi 4 に内蔵の mic が無い |
| P4 | 試験: host で PWM の値の変換と DMA の control block の輪、UAC の descriptor の解析。実機（Pi 4、T1、P3 と共有）: jack に繋いだ headphone で Music の音（ユーザーの UAT）、USB の mic で audio-loop の `--record`（WS204 の道具）の WAV |

## Phase（案）

| Phase | 内容 | 見積 | 依存 |
| --- | --- | --- | --- |
| p001 | この設計 | — | — |
| p002 | BCM2711 の DMA の driver と PWM の音の backend（P1）、Pi 4 の jack で鳴らす | 8 LW | p001 |
| p003 | USB の audio class（UAC1）の録音（P3） | 8 LW | ws197-p007a1 |
| p004 | HDMI の音（VCHIQ の音の service、P2） | 12 LW | p002（後回し可） |
| p005 | 実機の UAT と規約の全文 | — | p002、p003 |

## 判断（Q1 経由でユーザーへ、推し付き）

| ID | 問い | 推し |
| --- | --- | --- |
| F1 | Pi 4 で音を聞く所 | **3.5 mm の jack（headphone・speaker を繋ぐ）を先に**（P1）。HDMI の monitor の speaker で聞きたいなら P2（VCHIQ、大きい）を先に |
| F2 | 録音の機器 | **USB の mic か USB の headset（UAC1）**: 手元の機種を教えてほしい。xHCI の isochronous（ws197-p007a1）の後 |
| F3 | HDMI の音 | **後の Phase**（P2）。表示の firmware の KMS は変えない |
