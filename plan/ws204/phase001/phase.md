<!-- awesome-plan project=zedbsd record=ws204-p001 -->

# ws204-p001: HD Audio の録音の調査と設計

Parent: [WS204](../ws.md)
Status: in-progress（q929-i01、P1。設計を書いた、判断の点を Q1 へ）
Phase disposition: normal
Queue: q929（[P1 の lane](../../agents/P1/queue.md)）

## 由来

2026-10-11 ユーザー（P1 lane の承認）:「HD audioサウンドドライバの録音機能実装、HD audioのマイクとスピーカーを使った実際のHFP通話が目標。」

## 調べた事実（今の tree、2026-10-11 main e7ff23fba 以後）

- 録音の経路は**既にある**（ws035）: `src/drivers/pci/pci-hda.c` は ADC から入力の pin への道を探し（`hda_input_find`）、input の stream descriptor 0 で capture の BDL を動かす（`hda_ops_prepare/start/stop/position(capture=1)`）。`src/drivers/audio/audio.c` は capture の ring（read・mmap、`include/uapi/audio.h`）、audiod は `AUDIOD_CAPTURE` の stream（`userland/base/audiod/main.c`・`device.c`、共有 memory の ring）。QEMU の `hda-duplex`＋`audiodev none` で 48000 frame/秒の無音を確かめ済み（ws035 hda-design §7.2）。**実機で録った事は無い**。
- 5330 の音: Alder Lake-P の HDA（8086:51c8、Dell 1028:0b02）、codec は Realtek ALC3254（＋HDMI codec）。Linux は 5330 で legacy の HDA を選ぶ（SOF の DMIC の構成でない、ws100 p001）→ 内蔵マイクは HDA の codec の入力の pin（Realtek の digital mic の pin か analog の pin）にあるはず。どの pin かは未確認（codec の dump が要る）。
- 今の driver の入力の選び方と足りない所:
  1. **jack を先に選ぶ**（`hda_path_accepts` の pass 0 は jack だけ）。何も挿していない headset の jack（combo jack）を選び、内蔵マイクを使わない恐れが高い。jack の有無（pin sense、verb 0xF09 の bit 31、pin caps の presence detect）を見ていない。
  2. **mic bias（VRef）を出していない**（pin control は IN_EN だけ）。analog の electret mic（headset の mic）は bias 無しでは無音。
  3. **増幅は全部 0 dB**（`amp_caps & 0x7f` の offset）。pin の boost（Realtek の mic pin は 10 dB 刻みの入力 amp）も 0 dB のまま。内蔵マイクは小さすぎる恐れ。
  4. 入力の pin・ADC の様子を log に出さないので、実機で何を選んだか分からない。
- 録音を使う client は今は無い（audiod の capture を使う program が無い）。libkeiland の `kl_audio_stream_*` は再生だけ。

## 設計

| ID | 決め | 理由 |
| --- | --- | --- |
| R1 | 入力の pin は **capture の prepare のたびに**選ぶ: jack の pin で presence detect を持ち sense が「挿してある」なら jack（headset mic）、そうでなければ内蔵（default config の connectivity が internal、device が Mic In）、どちらも無ければ今の順（接続のある入力の pin） | 通話の始まりに headset を挿していればそれ、無ければ内蔵。jack の抜き差しの割り込み（unsolicited response）は今は扱わない（次の録音で反映） |
| R2 | analog の mic（default config の device が Mic In か Line In で、connection type が digital でない）の pin には pin caps の VRef のうち 80%→50%→100% の順で出す。digital の mic には出さない | electret mic の bias |
| R3 | 増幅: ADC の入力 amp は 0 dB、pin の入力 amp（boost）は **+20 dB に最も近い段**（amp caps の step size・offset から計算、無ければ 0 dB）。上げ下げの UI は今は作らない（UAT で足りなければ値を変える） | 内蔵マイクの感度は機種で違い、Linux の既定も boost を少し上げる。音量の UI は別 WS |
| R4 | attach と capture の prepare で `hda: input nid=.. config=0x........ caps=0x........ amp=0x........ sense=..` を 1 行ずつ kern の log に（入力の pin だけ）。選んだ道は `hda: capture path ...` | 実機で T1 が dmesg で確かめる |
| R5 | 確かめの道具 `audio-loop`（`userland/tests/`、製品の image に入れない）: audiod に再生と録音の stream を開き、1 kHz の正弦波を N 秒鳴らしながら録り、録った音の RMS と 1 kHz の Goertzel の大きさ（dBFS）、無音の区間の雑音を出す。`--record=秒 --wav=PATH` で録るだけ（UAT で人が聞く）。 | 5330 の内蔵 speaker → 内蔵マイクの音の輪で、人無しに録音を確かめられる（T1 が SSH で流す） |
| R6 | p007（WS197）の HFP の音は bluetoothd が audiod の client として再生（スマホの声）と録音（マイク）を 8/16 kHz・mono で開く（p001 の設計どおり）。audiod の変換はそのまま使う（WS197 p001 §7.4 の折り返しの filter は p007 の中） | WS204 は録音が実機で取れる所まで |

## Phase（案）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p001 | この設計 | — |
| p002 | pci-hda: R1〜R4（入力の選び直し・VRef・boost・log）と host の試験（mock の codec で選び方・VRef・boost の値） | p001 |
| p003 | `audio-loop`（R5）と Goertzel の host の試験、QEMU の `hda-duplex`（録音の速さ、無音の判定が「雑音だけ」になること）を T1 | p001 |
| p004 | 5330 の実機（T1）: `audio-loop` で speaker→内蔵マイク の 1 kHz が雑音より 20 dB 以上、dmesg の選んだ pin。UAT（ユーザー）: `--record` の WAV を聞く、headset を挿して録る | p002・p003 |
| p005 | 規約の全文の見直し | p004 |

## 判断（Q1 経由でユーザーへ、推し付き）

| ID | 問い | 推し |
| --- | --- | --- |
| U1 | 録音の音量（入力の gain）の UI を Settings の Sound に足すか | **今は足さない**（R3 の固定の boost。UAT で小さい・大きい時に値を直す。UI は別の WS） |
| U2 | 5330 の実機の確かめの形 | **T1 が SSH で `audio-loop`（内蔵 speaker の音を内蔵マイクで録る）**＋ユーザーが WAV を聞く UAT。人の声の確かめは HFP の通話の UAT（WS197 p007）でまとめて |
