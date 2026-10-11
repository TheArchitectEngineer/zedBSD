<!-- awesome-plan project=zedbsd record=ws205-p003 -->

# ws205-p003: libmedia の AAC-LC の encoder

Parent: [WS205](../ws.md)
Status: in-progress（q931、P1。実装・host 試験済み）
Phase disposition: normal
Queue: q931（[P1 の lane](../../agents/P1/queue.md)）

## 由来

2026-10-11 ユーザー「libmediaにAACエンコードの実装」、D3（推しどおり承認）: AAC-LC、TNS・PNS 無し、256 kbps まで、44.1/48 kHz。

## 実装

- `userland/desktop/libmedia/media-aac-encoder.h`・`aac-encode.c`（ISO/IEC 14496-3 の syntax から書いた。Huffman の book と scalefactor の帯は libmedia の decoder の規格の表 `aac-huffman.c`・`aac-bands.c` を使う）: 1・2 channel、32/44.1/48 kHz、bit rate 16 kbit/s × channel 〜 256 kbit/s。long window（sine）、MDCT は DCT-IV に畳んで 512 点の複素 FFT（host で直の式と照合して決めた）、帯ごとの mid/side（M S < L R / 8）、**rate の loop**: 係数 1 つあたりの雑音の水準を二分で探す（逆 water-filling: 決めた bit で SNR が最大、水準より小さい帯は送らない）、各帯の scalefactor は水準を超えない一番粗い物（値が 8191 を越えない範囲で二分）、sf の差は ±60 に収める、帯ごとに一番安い book（section は同じ book の続き）、bit の貯え（reservoir、6144 bit × channel まで）、それでも越える時は高い帯から黙らせる。bandwidth は channel あたりの bit rate で 12/15/17/20 kHz。出力は raw の access unit（SCE か CPE と END）、`media_aac_adts_header`（ファイル・試験）と `media_aac_audio_specific_config`（MP4・LATM、A2DP は p006）。
- libmedia の `Makefile` に `aac-encode.c`、`exports.map` に encoder の 5 つ。

## 確認

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws205/tests/run-aac-host-test.sh`（FFmpeg の decoder を独立の側に、libmedia の decoder（`plan/ws202/tests/host-aac-native.c`）も、ASan/UBSan の encoder で） | **aac: all PASS**。bit rate は目標の 97〜103%。FFmpeg の decode の SNR（入力と、1024 の遅れ）: 和音 48k stereo 256k 80.2 dB・128k 71.2、44.1k 192k 79.4、32k mono 64k 74.7、雑音と click の「music」48k stereo 256k 22.8・44.1k mono 96k 19.3（同じ入力で FFmpeg の encoder は 17.7・13.1）、矩形波 24.6、無音は無音。libmedia の decoder と FFmpeg の decoder の差は全部 135 dB（同じ PCM）。速さ: 4 秒の 48k stereo を 0.26 秒（host） |
| `make … BUILD=build/ws157-q937 … dynamic/libmedia.so` | rc 0、warning 0、encoder の 5 つを export |
| `plan/tools/style-check.py` aac-encode.c・media-aac-encoder.h | 0 |

残り（Future Work の候補）: **短い window（block switching）**が無い: 強い transient の前に pre-echo が出うる（256 kbps では小さい）。headset の UAT で聞こえたら足す。心理音響の重みは無く SNR 最大の配分（高い bit rate 向き）。TNS・PNS は D3 で作らない。
