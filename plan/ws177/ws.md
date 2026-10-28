<!-- awesome-plan project=zedbsd record=ws177 -->

# WS177: ベータ2 積み残し（準正常系・異常系）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001〜p018・p020〜p046 cleared、p002・p003・p006・p011 は 2026-10-10 ユーザーの 5330 の UAT「WS177 はOK」。p019 の残り（Browser の IME (b)(d)）は 2026-10-08 ユーザー「ブラウザはベータ3に移します」で WS の外へ）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: MG002・MG003・MG005
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

第 1〜3 段の実装で、正常系だけを実装して残した準正常系・異常系を、ここに Phase として集めて実装する。完了の条件は、集めた Phase が全て cleared（host の試験と T1 の試験）。

## ユーザーの決定

- 2026-10-06 夜「ベータ1のWS＋ベータ2の一部実装をまずすべて実装してください。…正常系が通ればいいです。準正常系と異常系で未実装な部分は、そういった積み残しを管理するWSを作って、そこにPhaseとして積みましょう。それはすべての実装が1パス通って疎通確認できてからでいいです。」「積み残しWSはベータ2積み残しという形でWSを作りましょう。」
- 2026-10-06 夜「各WSに入れるとWSがCompleteにできないので、専用の1つのベータ2積み残しというWSに入れてください。」→ 積み残しは元の WS・Phase に置かず、この WS の一覧（[backlog-p1.md](backlog-p1.md)・[backlog-p2.md](backlog-p2.md)）にだけ書いた。Phase への分け方は [phasing-20261008.md](phasing-20261008.md)。
- 2026-10-08（p037 の整列の記憶）ユーザーの決定 (b): 同じ desktop で同じ形を選び直すと前の窓を前の枠へ、compositor の中だけ。
- 2026-10-08「ブラウザはベータ3に移します」（p019 の残り）。
- 2026-10-10（5330 の UAT）「WS177 はOK」。

## 結果

- 準正常系・異常系の積み残しを 45 の Phase（案 A〜U ほか）で実装した: compositor の wl_surface.enter・leave、i915 TC の PHY の待ち、UCSI の回復、desktop の UI の小物、通知とメールの通知の口の堅さ、fidoctl の PIN、Welcome、Files の Recents、手書き、keiland-preview、Save Clean Copy、Notes の文字の box、field・text area の undo・redo、Browser の sign-in code、Mail の操作と IMAP・SMTP の互換、音楽の collection と再生の失敗、printd・backend・Settings の printer、通知の頁、mediafile の fragmented MP4・MPEG-TS・Ogg・AVI と libmedia、上端の帯と整列の枠と Home、libpdf の form XObject の文字と PDF Viewer の検索・選択、IPv6 の libc・networkd・DHCPv6。
- 証拠は host の試験と T1 の QEMU（T1-404〜T1-500 の各試験）、p002・p003・p006・p011 はユーザーの 5330 の UAT「WS177 はOK」。

## 制限・移管

- p019 の (b)(d)（Browser の IME・form の残り）は期待どおりでないまま、ユーザー「ブラウザはベータ3に移します」で WS の外へ（Browser 本体は [WS074](../ws074/ws.md)）。
- 実機の UAT が未実施の物: p009（手書き）・p012 の指・p015 の新しい UI・p032 の実の printer の IPP。p012 の文字の box の回転は未実装（backlog）。
- p039 は番号を使わなかった。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws177-p001 | 案 E: compositor の wl_surface.enter・leave | cleared（2026-10-08、T1-404） |
| ws177-p002 | 案 F: i915 の TC の legacy の PHY の待ち | cleared（2026-10-10、ユーザーの 5330 の UAT） |
| ws177-p003 | 案 G: UCSI の誤り・CANCEL・PPM_RESET の回復ほか | cleared（2026-10-10、ユーザーの 5330 の UAT） |
| ws177-p004 | 案 D: desktop の UI の小物 | cleared（2026-10-08、T1-406・T1-413） |
| ws177-p005 | 案 A: 通知とメールの通知の口の堅さ | cleared（2026-10-08、T1-408） |
| ws177-p006 | 案 I: fidoctl の PIN の入力 | cleared（2026-10-10、ユーザーの 5330 の UAT） |
| ws177-p007 | 案 B: Welcome の準正常系 | cleared（2026-10-08、T1-410） |
| ws177-p008 | 案 C: Files の Recents の仕上げ | cleared（2026-10-08、T1-412） |
| ws177-p009 | 案 H: 手書きの頑健さ | cleared（2026-10-08、host の範囲、実機の UAT は未実施） |
| ws177-p010 | 案 J: keiland-preview の仕上げ | cleared（2026-10-08、T1-414） |
| ws177-p011 | 案 K3: Save Clean Copy の仕上げ | cleared（2026-10-10、ユーザーの 5330 の UAT） |
| ws177-p012 | 案 K: Notes の文字の box | cleared（2026-10-08、T1-415。指は実機の UAT、回転は未実装） |
| ws177-p013 | 案 K の 9: field・text area の undo・redo・clipboard | cleared（2026-10-08、T1-416） |
| ws177-p014 | 案 N4: Browser の sign-in code | cleared（2026-10-08、T1-418） |
| ws177-p015 | 案 N2: Mail の基本の操作 | cleared（2026-10-08、host と T1-420） |
| ws177-p016 | 案 N: Mail の IMAP・SMTP の互換 | cleared（2026-10-08、T1-422） |
| ws177-p017 | libbrowser の `browser_view_focus_field` | cleared（2026-10-08、T1-424） |
| ws177-p018 | sign-in code を one-time-code の欄に直に | cleared（2026-10-08、T1-424） |
| ws177-p019 | 案 O: Browser の IME・form と shell の fd | test-wait のまま WS を閉じた（T1-425 で (a)(c)(e) は確認、(b)(d) は期待どおりでなく、ユーザー「ブラウザはベータ3に移します」） |
| ws177-p020 | 案 M の 1: 音楽の collection と cover | cleared（2026-10-08、T1-450） |
| ws177-p021 | 案 M の 2: 音楽の再生の失敗と key | cleared（2026-10-08、T1-456） |
| ws177-p022 | 案 Q の 1: printd の同時の送信・上限・取り消し | cleared（2026-10-08、T1-457） |
| ws177-p023 | 案 Q の 2: backend の printd の寿命 | cleared（2026-10-08、T1-457） |
| ws177-p024 | 案 Q の 3: 設定の writer・title の UTF-8・protocol の試験 | cleared（2026-10-08、T1-457） |
| ws177-p025 | 案 Q の 4: Settings で printer を変える | cleared（2026-10-08、T1-465） |
| ws177-p026 | 案 A2: Settings の Notifications の頁 | cleared（2026-10-08、T1-461） |
| ws177-p027 | 案 T の 1: fragmented MP4 と壊れた index | cleared（2026-10-08、T1-469） |
| ws177-p028 | 案 T の 2: MPEG-TS の reader | cleared（2026-10-08、T1-469） |
| ws177-p029 | 案 T の 3: Ogg の reader | cleared（2026-10-08、T1-469） |
| ws177-p030 | 案 T の 4: AVI の reader | cleared（2026-10-08、T1-469） |
| ws177-p031 | 案 T の 5: videoplayer・Music を libmedia へ | cleared（2026-10-08、T1-469） |
| ws177-p032 | printd の IPP の chunked ほか（BUG-271） | cleared（2026-10-08、T1-470。実の printer は未実施） |
| ws177-p033 | 案 U の 1: 上端の帯の長押し | cleared（2026-10-10、T1-489） |
| ws177-p034 | 案 U の 2: 全画面の app へ帯の press | cleared（2026-10-10、T1-489） |
| ws177-p035 | 案 U の 3: 整列の枠と letterbox | cleared（2026-10-10、T1-489） |
| ws177-p036 | 案 U の 4: 出力の変更・メニュー・swap の取り消し | cleared（2026-10-10、T1-489） |
| ws177-p037 | 案 U の 5: 整列の記憶と Super+矢印 | cleared（2026-10-10、T1-489） |
| ws177-p038 | 案 U の 6: Home の fade・pill・白の文字 | cleared（2026-10-10、T1-489） |
| ws177-p040 | 案 L の 1: libpdf の form XObject の文字 | cleared（2026-10-09、T1-474） |
| ws177-p041 | 案 L の 2: PDF Viewer の検索の一致の規則 | cleared（2026-10-09、T1-474） |
| ws177-p042 | 案 L の 3: 選択 | cleared（2026-10-09、T1-474） |
| ws177-p043 | 案 L の 4: 窓の中の検索の欄 | cleared（2026-10-09、T1-474） |
| ws177-p044 | 案 R2: IPv6 の libc と道具 | cleared（2026-10-09、T1-476） |
| ws177-p045 | 案 R: networkd の IPv6 | cleared（2026-10-09、T1-500） |
| ws177-p046 | 案 R: dhcpc の DHCPv6 | cleared（2026-10-09、T1-500） |

## 記録の所在

Phase の計画・結果・試験は削除した。git の commit `fb1f6afee` の `plan/ws177/` にある。Queue の履歴は [plan/history](../history/index.md)。
