<!-- awesome-plan project=zedbsd record=ws197-p006 -->

# ws197-p006: HFP の制御（HF の SLC、AT、着信・発信・応答・終話・DTMF・音量・音声認識）

Parent: [WS197](../ws.md)
Status: in-progress（q928-i01、P1）
Phase disposition: normal
Queue: q928（[P1 の lane](../../agents/P1/queue.md)）

## 由来・承認

2026-10-11 ユーザー（P1 lane の承認の原文）:「HFPの実装(p006, p007)、HD audioサウンドドライバの録音機能実装、HD audioのマイクとスピーカーを使った実際のHFP通話が目標。」
設計の大枠は [p001](../phase001/phase.md) §7.3・§8・§11（Q7・Q11・Q13 は推しどおり、2026-10-09 ユーザー）。この phase.md はその詳細設計（P1、2026-10-11）。音（SCO・CVSD・mSBC・audiod）は p007。

## 範囲

- bluetoothd: `at.c`（AT の応答の行の分け方と解析）、`hfp.c`（HF の profile、phonemux の 3 番目の子）、HF の SDP の record、PHONE の request（DIAL・ANSWER・HANGUP・DTMF・VOLUME・VOICE・HOLD）と event（PHONE CALL・PHONE HF）。
- desktop の経路: libkeiland-backend（zedBSD は bluetoothd の socket、Linux・FreeBSD は ENOTSUP）→ compositor `phone-shell.c`（phone の version 33）→ libkeiland の公開 API（KL_VERSION 87）→ Phone の app（着信の画面・通話中の画面・発信）。
- 範囲の外: 音（p007）、codec の交渉と mSBC（p007b。HF の features で codec negotiation を出さず、AG は CVSD を使う）、iPhone の確かめ（p008）。

## 詳細設計

### D1 HF の features（AT+BRSF と SDP の 0x0311）

| 値 | bit | 理由 |
| --- | --- | --- |
| AT+BRSF（HFP 1.8 の HF の bit） | 3-way calling（1）、CLI presentation（2）、voice recognition（3）、remote volume control（4）、enhanced call status（5） → `AT+BRSF=62` | 着信の番号・待ち受け・保留・CLCC・音量・音声認識。EC/NR（0）は作らない（echo は AG に任せる、p001 Q7）。codec negotiation（7）は mSBC の後（p007b）。HF indicators（8）・eSCO S4（9）は p007 の後 |
| SDP の SupportedFeatures | 3-way（1）、CLI（2）、voice recognition（3）、remote volume（4） → 0x001E | SDP の bit の割り当ては BRSF と違う（wide band は 5、p007b） |
| 版 | HFP 1.8（0x0108）、service class 0x111E と 0x1203 | p001 §4.4 |

### D2 SLC（HFP 1.8 §4.2）

順に 1 つずつ（前の OK を待つ）: `AT+BRSF=62` → `+BRSF:<AG>`・OK → `AT+CIND=?`（indicator の名前と順を覚える: call、callsetup、callheld、service、signal、roam、battchg）→ `AT+CIND?`（今の値）→ `AT+CMER=3,0,0,1`（OK で SLC 完了）→ AG と HF が 3-way を持てば `AT+CHLD=?` → SLC の後: `AT+CLIP=1`、`AT+CCWA=1`（3-way の時）、`AT+CMEE=1`、`AT+CLCC`（今の通話の同期）。どれかが ERROR・5 秒の無応答なら失敗（CLIP・CCWA・CMEE の ERROR は無視して続ける）。

### D3 接続の向き

- HF から: link が ready・持ち主が calls を on → AG の SDP（0x111F）の RFCOMM channel と SupportedFeatures → DLC を開けて SLC。
- AG から: 自分の HF の server channel（`BTD_PHONE_HF_CHANNEL` 17、SDP の record が示す）への DLC を受ける（HF から開けている途中なら AG の方を受けて自分の方は閉じる）。
- 失敗の再試行: PBAP と同じ形（30 秒から倍、最大 10 分。AG が無い（no-ag）はその link で再試行しない）。

### D4 通話の状態

- indicator（call・callsetup・callheld）と `RING`・`+CLIP`・`+CCWA`・`AT+CLCC` の行から、最大 2 本の通話（index 1・2）の表を作る。各通話: index、方向（in・out）、状態（incoming・dialing・alerting・active・held・waiting）、番号、名前（`+CLIP` の alpha、無ければ空）。
- enhanced call status: indicator が変わるたびに `AT+CLCC` を出し、表は CLCC で決める（CLCC の行が無い・ERROR なら indicator から推す）。
- 1 つの変化ごとに PHONE CALL の行を購読者へ（同じ内容は出さない）。

### D5 bluetoothd の request と event（持ち主と root）

| request | AT | 答え |
| --- | --- | --- |
| `PHONE DIAL to="<番号>"` | `ATD<番号>;` | OK → DONE、ERROR → `ERROR refused`、SLC 無し → `ERROR not-ready` |
| `PHONE ANSWER` | `ATA` | 同上 |
| `PHONE HANGUP` | `AT+CHUP`（着信の拒否も同じ） | 同上 |
| `PHONE HOLD action=0\|1\|2` | `AT+CHLD=<n>`（0: 待ちを拒否・保留を切る、1: 今を切り次を、2: 保留と切り替え） | 3-way が無ければ `ERROR no-hold` |
| `PHONE DTMF key=<0-9*#ABCD>` | `AT+VTS=<key>` | 通話中だけ |
| `PHONE VOLUME speaker=<0..15>` | `AT+VGS=<n>` | |
| `PHONE VOICE on=0\|1` | `AT+BVRA=<n>` | AG が持たなければ `ERROR no-voice` |

event（購読者へ）: `PHONE HF state=off|connecting|ready|failed why=<語> service=0|1 signal=0..5 battery=0..5 roam=0|1 speaker=0..15 voice=0|1 hold=0|1`（変わった時）、`PHONE CALL index=<n> state=<incoming|dialing|alerting|active|held|waiting|ended> dir=in|out number="..." name="..."`（変わった時。終わった通話は ended を 1 回）。PHONE SHOW・STATE の行に `calls=<off|connecting|ready|failed>` を足す。

### D6 desktop の経路

- backend（keiland-backend.h）: `kl_backend_phone_dial`・`_answer`・`_hangup`・`_hold`・`_dtmf`・`_volume`・`_voice`（kl_backend_phone_send と同じく id と result）、`struct kl_backend_phone_call`（index・state・dir・number・name）を item と同じ形で取る `kl_backend_phone_take_call`、state に hf の値（hf・service・signal・battery・speaker・voice・hold）。Linux・FreeBSD は ENOTSUP。
- compositor（phone の version 33）: request 10 `answer(uint request)`、11 `hangup(uint request)`、12 `hold(uint request, uint action)`、13 `dtmf(uint request, string key)`、14 `speaker(uint request, uint level)`、15 `voice(uint request, uint on)`、既存の 2 `call(uint request, uint channel, string to)` は bluetooth の backend なら DIAL。event 11 `call(uint index, uint state, uint direction, string number, string name)`、12 `hf(uint state, uint service, uint signal, uint battery, uint speaker, uint flags)`。phone の program（app_id phone）だけ。listen していない時の着信は compositor が通知（既存の phone の通知の口）を出し、押せば Phone を開く。
- libkeiland（KL_VERSION 87）: `kl_system_phone_answer`・`_hangup`・`_hold`・`_dtmf`・`_speaker`・`_voice`、`kl_system_take_phone_event` の新しい種類 `KL_PHONE_CALL`（`struct kl_phone_event` に call の欄）と `KL_PHONE_HF`。
- Phone: 着信の card（相手の名前・番号、Answer・Decline）、通話中の帯（相手、経過時間、Hang Up、Keypad（DTMF）、保留・切り替え（2 本の時））、会話の Call button は bluetooth の backend なら DIAL、終わった通話は timeline の Kind: call（PBAP の履歴と重ねる、p001 §8）。

## 判断（P1、変えたい時は Q1 経由で）

| ID | 決め | 理由 |
| --- | --- | --- |
| H1 | codec negotiation を今は出さない（CVSD だけ） | mSBC は p007b。出すと AG が +BCS を送り、音の無い今は答えられない |
| H2 | EC/NR は作らず AT+NREC を送らない（AG に任せる） | p001 Q7 |
| H3 | 着信の通知は既存の phone の通知の口（listen していない時） | 新しい通知の口を作らない |
| H4 | 通話の表は CLCC を正とし、indicator は CLCC が無い AG の予備 | enhanced call status を持つ AG（Android・iPhone）が多い |

## 試験

- host: `plan/ws197/tests/bt-hfp-host-test.c`（台本の AG: SLC の順、CIND の順の違い、着信 RING・+CLIP→ANSWER→active→HANGUP、発信、3-way の待ちと切り替え、CLCC 無しの AG、無応答の timeout、AG からの DLC、知らない応答・長すぎる行の捨て、PHONE の request の答え）、at の解析の試験（fuzz 風の入力）。phone-backend・phone-shell の試験に call を足す。
- 実機（p008 と同じ、ユーザーの UAT）: 5330＋Android で着信・応答・終話・発信・DTMF。音は p007。

## 進み（q928-i01）

### 1. bluetoothd の HF（済み）

- 新 `userland/base/bluetoothd/at.c`・`at.h`（行の切り分け・結果の欄・数・indicator の名前）、`hfp.c`・`hfp.h`（D1〜D5）。phonemux の 3 番目の子（main.c）、HF の SDP の record（phone.c、channel 17、calls が on の時だけ）、PHONE DIAL・ANSWER・HANGUP・HOLD・DTMF・VOLUME・VOICE、subscribe の時に PHONE HF と通話の行、PHONE SHOW・STATE の行に `calls=`。Makefile に 2 file。
- 確認: 新 `plan/ws197/tests/bt-hfp-host-test.c`（143 checks: at の解析、SLC の順、着信→応答→DTMF→終話（ended は 1 回）、発信、無応答の timeout、拒否の語、SLC の拒否と再試行、CLCC の無い AG の推測、link の終わり）を `bt-phone-host-test.sh` に足して全 PASS（ASan/UBSan）。zedBSD の bluetoothd の named build warning 0。style 新規 0。

### 2. desktop の経路（済み）

- backend（keiland-backend.h）: `kl_backend_phone_call_ask`（PHONE DIAL・ANSWER・HANGUP・HOLD・DTMF・VOLUME・VOICE、引数は数字・+*#ABCD だけ）、`kl_backend_phone_take_call`（PHONE CALL の変化の ring）、`kl_backend_phone_get_hf`（PHONE HF）、CHANGED_CALL・CHANGED_HF。events の接続が切れたら HF は off。Linux・FreeBSD は ENOTSUP。
- compositor（phone の version 33、manager 33）: request 10 `control(request, ask, argument)`（phone の program だけ、bluetooth の backend だけ、done で答える）、event 10 `call`・11 `hf`（listen している phone の program の version 33 へ、listen した時に今の HF と通話も）。listen している phone の program が無い時の着信は通知（「Incoming call」、押せば Phone）。HF が ready でなければ通話の表を空に。
- libkeiland（KL_VERSION 87）: `kl_system_phone_calls`・`kl_system_phone_hf`・`kl_system_phone_control`、phone event `KL_PHONE_CALLS_CHANGED`・`KL_PHONE_HF_CHANGED`、`struct kl_phone_call`・`kl_phone_hf`（既存の `kl_phone_event` は変えない）。
- Phone: 通話の card（窓の上、鳴っている時は Answer・Decline、通話中は経過時間・Hang Up・Keypad（DTMF）・2 本で Swap）、会話の Call は paired なら HF の DIAL（HF が ready でなければ notice）、通話の時間で 1 秒ごとに再描画。

確認: phone-shell・phone-backend の試験に通話を足して PASS、phone-store・run-host-mediastorage PASS、zedBSD の phone・wayland・libkeiland.so・bluetoothd・settings、Linux の libkeiland.so・wayland の named build warning 0、style 新規 0、exports --check・boundary PASS。

未実施（ユーザーの UAT、5330＋Android、音は p007）: Settings の Bluetooth で phone の calls を on → Phone を開いて着信を待つ → card に名前・番号、Answer で通話中・時間が進む、Keypad で DTMF（自動音声で確かめる）、Hang Up。Phone を閉じた状態の着信で通知。会話の Call で発信。
- Settings（ebf270695）: capability `KL_SYSTEM_HAS_PHONE_HF`、「Use as phone」で calls も on（compositor が持つ時）、既に使っている phone には「Also use contacts」の次に「Also use calls」。zedBSD・Linux の settings の build warning 0。
- Status: 実装済み・UAT（実機）待ち。音は p007。
