<!-- awesome-plan project=zedbsd record=ws205-p001 -->

# ws205-p001: Bluetooth audio の調査と設計

Parent: [WS205](../ws.md)
Status: in-progress（q931-i01、P1。設計を書いた、判断の点を Q1 へ）
Phase disposition: normal
Queue: q931（[P1 の lane](../../agents/P1/queue.md)）

## 由来

2026-10-11 ユーザー:「Bluetoothオーディオの実装、libmediaにAACエンコードの実装、SBCエンコードの実装、SBCとAACでBlueoothオーディオを使えるようにする。目標はPCに接続されたBluetoothヘッドセットで、スマホ経由のHFP通話できること。」

## 今の tree の事実（2026-10-11）

| 所 | 事実 |
| --- | --- |
| libmedia | 自前の **AAC-LC の decoder** がある（`aac*.c`、約 4,800 行: ADTS・raw の frame、Huffman の表、帯、IMDCT の synth）と H.264。**SBC は無い**（encoder も decoder も）。**AAC の encoder は無い**。公開の API は再生の engine（`media.h`）だけで、codec 単体の API は無い |
| bluetoothd | host stack は userland（WS143）。L2CAP（任意の PSM の connect・accept、`l2cap.h`）、RFCOMM、SDP の client と server（`sdp.c`・`sdps.c`）、HFP の HF（ws197-p006）、MAP・PBAP、HID。**AVDTP・A2DP・AVRCP・HFP の AG は無い**。SCO は ws197-p007（kernel の isochronous が先） |
| audiod | 出力は HDA の device 1 つ（`device.c`）、mix の拍は device の period。Bluetooth へ出す口は無い |
| Settings | Bluetooth の page で device の pairing・接続（WS143）、phone の役（ws197）。audio の device の役は無い |
| 規約 | GPL の code を写さない。BlueZ の SBC（LGPL）・FFmpeg（LGPL）も写さず、仕様（A2DP 1.3 の付録 B の SBC、ISO/IEC 14496-3 の AAC、RFC 3016 の LATM）から書く。FFmpeg は host の試験の照合の道具としてだけ使う（製品に入れない、ws197-p001 §7.4 と同じ） |

## 経路

```
app → audiod（mix）──(HDA)──────────────────────────→ 内蔵 speaker
                   └─(Bluetooth の出力の口 A1)→ bluetoothd → SBC/AAC の encode → AVDTP の media channel → headset（A2DP）

スマホ(AG) ⇄ SCO ⇄ bluetoothd [HF（ws197-p006/p007）] ⇄ [AG（新）] ⇄ SCO ⇄ headset(HF)
              通話の制御: phone の +CIEV・RING・+CLIP → headset の +CIEV・RING・+CLIP、headset の ATA・AT+CHUP・button → phone の ask
```

## 設計

| ID | 決め | 理由 |
| --- | --- | --- |
| C1 | libmedia に codec 単体の公開 API を足す: `media_sbc_encoder_*`・`media_sbc_decoder_*`（A2DP の SBC と、mSBC（16 kHz・mono・8 subband・15 block・bitpool 26）の両方）、`media_aac_encoder_*`（AAC-LC、出力は raw の access unit、LATM・ADTS の包みは別の関数）。今の decoder の内部（Huffman の表・帯の表）を encoder と共有 | ws197-p007b2（mSBC）と A2DP が同じ codec を使う。表の二重持ちを避ける |
| C2 | SBC の encoder: 4・8 subband の多相の分析の filter（仕様の係数）、Loudness と SNR の bit 割り当て、joint stereo（subband ごとに判定）、CRC-8。A2DP の既定は 44.1/48 kHz・joint stereo・8 subband・16 block・Loudness・bitpool 53（High Quality、約 345 kbps）、headset の能力の範囲で。decoder は mSBC の受けと試験のため | 仕様どおり。bitpool は相手の最大で切る |
| C3 | AAC の encoder: AAC-LC、long・short の window の切り替え（過渡の検出）、MDCT（decoder の IMDCT の逆）、簡単な心理音響（帯ごとのマスキングのしきい値、拡散と絶対の聴覚のしきい値）、帯ごとの scalefactor と全体の rate の loop、Huffman の codebook の選択（decoder の表）、M/S stereo。TNS・PNS・LTP は作らない。目標 256 kbps（相手の最大で切る）、44.1/48 kHz | headset の AAC は LC。256 kbps なら簡単な心理音響で十分な質（音質の目標は「SBC の bitpool 53 より悪くない」） |
| C4 | 試験: codec ごとに host の試験。SBC は自前の decoder と FFmpeg の decoder（host の道具）で同じ PCM、正弦波・雑音・音楽の断片の SNR（bitpool 53 で 1 kHz の SNR ≥ 60 dB 程度、値は p002 で決める）。AAC は自前の decoder と FFmpeg で decode して SNR と NMR の簡易の値、bit rate の上限を守ること。fuzz の decoder の試験（壊れた frame） | ユーザーの条件「decode の一致」 |
| A1 | **audiod に Bluetooth の出力の口**: audiod の protocol に `AUDIOD_SINK` の向きの stream を足し、bluetoothd が「出力の device」として登録する。出力が Bluetooth の時、audiod の mix の拍は sink の読みの位置（bluetoothd の encoder の送りの拍）で進む（HDA の period の代わり）。出力の切り替えは audiod の request（`AUDIOD_OUTPUT`）、Settings の Sound の「出力」に headset が出る。headset が切れたら HDA に戻す | app は今のまま（audiod の client）。全部の音が headset へ行く。clock の主は headset の送りの拍 |
| A2 | bluetoothd の A2DP の source: SDP の Audio Source の record、AVDTP 1.3 の signaling（PSM 25: Discover、Get All Capabilities、Set Configuration、Open、Start、Suspend、Close、Abort、Delay Report を受ける）、media の channel（RTP の header ＋ SBC の media payload header と frame、AAC は LATM の AudioMuxElement）、L2CAP の MTU に詰める、送りの拍は monotonic の clock で 20 ms ごと（出遅れは frame を詰めて送る、溜まりすぎは捨てる）。codec は相手が AAC を持てば AAC、無ければ SBC（D5） | 仕様（A2DP 1.3・AVDTP 1.3） |
| A3 | AVRCP: 1.6 の Absolute Volume（Target の側の SetAbsoluteVolume・RegisterNotification）だけを p007 で。headset の音量の button と Sound の音量を合わせる。再生・停止の button（PASS THROUGH）は Music の app に後で | 最小で効く物 |
| H1 | headset の HFP: bluetoothd に **AG の役**（RFCOMM の server、SDP の Handsfree Audio Gateway の record、AT の受け: BRSF・BAC・CIND・CMER・CHLD・CLIP・CCWA・NREC・VGS・VGM・BVRA・BCS、結果: +CIEV・RING・+CLIP・+CCWA・+BCS）。`at.c` の行の切り出しを共有 | |
| H2 | 通話の橋渡し: phone の HF の状態（ws197-p006 の calls・indicators）を headset の AG に写す。headset の操作（ATA・AT+CHUP・AT+CHLD・ATD は最後の番号の再発信 AT+BLDN）を phone の ask に。どちらかが切れたら通話は phone か PC（ws197-p007 の内蔵）へ戻す | ユーザーの目標「headset でスマホ経由の通話」 |
| H3 | 音の橋渡し: phone の SCO と headset の SCO を **bluetoothd の SCO の thread で直に繋ぐ**（audiod を通さない）。両方 CVSD なら copy、片方が mSBC（16 kHz）なら decode・8↔16 kHz の変換・encode。kernel の SCO の口は 2 本（alternate は CVSD 16 bit の 2 本で alt 4）。通話の間は A2DP を Suspend | 遅れが最小。audiod の rate 変換を通らない |
| H4 | 依存: H3 は ws197-p007a（xHCI の isochronous・SCO の口）と p007b1（SCO の thread）。A1〜A3・C1〜C4 は SCO が要らず**先に進められる** | |

## Phase（案）

| Phase | 内容 | 見積 | 依存 |
| --- | --- | --- | --- |
| p001 | この設計 | — | — |
| p002 | libmedia の SBC の encoder・decoder（A2DP と mSBC）と host の試験（FFmpeg の照合） | 6 LW | p001 |
| p003 | libmedia の AAC-LC の encoder（C3）と host の試験 | 15 LW | p001 |
| p004 | audiod の Bluetooth の出力の口（A1）と Settings の Sound の出力の選択 | 6 LW | p001、D2 |
| p005 | bluetoothd の A2DP の source（SBC）、Settings の Bluetooth の headset の接続 | 10 LW | p002、p004 |
| p006 | A2DP の AAC（LATM、codec の選択） | 3 LW | p003、p005 |
| p007 | AVRCP の Absolute Volume | 4 LW | p005 |
| p008 | HFP の AG と通話の制御の橋渡し（H1・H2） | 8 LW | ws197-p006 |
| p009 | SCO の音の橋渡し（H3、CVSD と mSBC） | 5 LW | p002、p008、ws197-p007a・p007b1 |
| p010 | 実機（T1）: headset で音楽（SBC・AAC）、headset で通話（ユーザーの UAT） | — | p006、p009 |
| p011 | 規約の全文の見直し | — | p010 |

計 約 57 LW（ws197-p007 の約 27 LW は別）。

## 判断（Q1 経由でユーザーへ、推し付き）

| ID | 問い | 推し |
| --- | --- | --- |
| D1 | 試験に使う headset（機種、AAC と mSBC を持つか） | **ユーザーの手元の headset の機種を教えてほしい**。AAC の確かめには AAC を持つ物（多くの耳の headset が持つ）。無ければ SBC だけ実機、AAC は host の試験まで |
| D2 | 出力の切り替えの作り | **audiod に Bluetooth の出力の口（A1）**。全部の音が headset へ。案: app ごとの行き先（大きい、今は要らない） |
| D3 | AAC の encoder の範囲 | **AAC-LC、TNS・PNS 無し、256 kbps まで、44.1/48 kHz**（C3） |
| D4 | AVRCP | **Absolute Volume だけ**（A3）。再生の操作は後 |
| D5 | codec の優先 | **AAC を持つ headset は AAC、無ければ SBC（bitpool 53）**。Settings で SBC に固定できる選択は後 |
| D6 | 順 | **codec（p002・p003）と A2DP（p004〜p007）を先**、headset の通話（p008・p009）は ws197-p007（SCO の kernel）の後 |
