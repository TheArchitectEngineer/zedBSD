<!-- awesome-plan project=zedbsd record=beta2-triage -->

# ベータ2 の残り作業（2026-10-11 Q1 更新）

公開は 10/17（OSC 当日、朝から会場）なので、**10/16 中にリリースの準備を終える**。
体制（2026-10-11 ユーザー）: Q1＋P1（新規の実装）・P2（Bug）・P3（Pi 4 の VC6 の描画）＋T1（試験）＋ユーザー（UAT・判断）。WS203（Pi 4 の Ethernet）は Codex の別のセッション。この表は計画で、Queue の承認ではない。
見積もりは LW（1 LW ≈ エージェントの実時間 20 分）。完了した項目は消す。Q1 は進むたびに更新する。

## 日程

| 日 | やること |
| --- | --- |
| 10/10〜10/12 | 走っている Bug の直しと試験を終える → WS199（セキュリティキー）→ WS200 |
| **10/13** | 機能の凍結の目標。ベータなので UAT の Bug は直し切れなくてよく、code freeze はぎりぎりまで行わないこともある |
| 10/14〜10/15 | 最終回帰（QEMU）と 5330 の UAT、出た Bug を「直す／既知の問題に書く」で仕分け |
| **10/16** | 最終の image・配布物・license の一覧・release notes を確定。Vulkan Video は既定で ON（2026-10-11 ユーザー）。公開の手順の確認（公開はユーザーの指示で） |

## 今の状況

- 2026-10-11: [WS197](ws197/ws.md)（スマホ連携）は SMS・MMS の受信と表示（日本語・写真を実機で確認済み）、PBAP の連絡先と通話の履歴、Bluetooth の自動復旧まで main に入った。P1 の書きかけの差分は照合して必要な 1 点だけ merge（d5296c23e）。残りは HFP（通話）と MMS の写真・動画の送信。**HFP をベータ2 に入れるかはユーザーの判断待ち。**
- [BUG-287](bugs/BUG-287.md)（スマホ連携が「connecting」のまま）の直しは全部 main に入った。image を作り直してよい。
- WS199・WS200 は実装と QEMU の試験が済み、残りは次の UAT。
- WS083（Vulkan Video）は既定で ON（2026-10-11 ユーザー）。残りは p007 の hang の実機の確かめ。[WS202](ws202/ws.md)（libmedia だけで mp4・m4a）は software を main に統合済み、実機と受け入れの全項目が未確認。
- Raspberry Pi 4: [WS141](ws141/ws.md) の VC6 の描画の改善を P3 が Codex から引き継いだ（mouse の移動 300 ms → 33 ms → 16 ms）。
  - 2026-10-12 夜（ユーザーの実機）: App Home を開くアニメが約 10 コマ（WS211 のクロスフェード、4c3a56cf9）、Files・Settings・Music を起動していても App Home の上のマウス移動が 60fps（seat の pointer_taken 93701e2ef、Settings・Files の無操作の描画と起床の直し 45cb43ecd、Music の部分の描き直し 784e6c5a5）、App Home の開閉で落ちない、Music は他の app があっても起動（Vulkan の予算 512MiB・準備の失敗の局所化）。残り: デスクトップの CPU の負荷の瞬間の引っかかり（CPU 1 つ、SMP WS208）、30 コマへは draw の束ね（WS211 p003）・SMP・kernel の準備の mutex（WS141 (b)）。
  - 2026-10-12: App Home の遷移 9 秒 → 1 秒（HAL の uncached の unmap の TLB の flush を 1 回 b17a753d4、kern_free の O(1) 4d3d7eddf、draw の pool）、drag は非常に速く、Settings の present-copy 305 → 2〜15 ms。3 回目の App Home の compositor の終了（record の上限 1MiB）と preview の hover の全面の再合成を直した（main f07c345d3、実機の確認待ち）。vkQueueSubmit の非同期化 [ws141-p016](ws141/phase016/phase.md) は review-4 を通り A1・A2 を実装、2026-10-12 ユーザーの判断で実機の確認の前に main に merge（d9e0c35af、今回の image で確かめる）、A3（lazy map）が続く。App Home の hover は変わったアイコンだけの damage（efe8f8b63）。
- WS157 Photos の削除の統合は P1 の q924。WS202 の再生の実機・QEMU の確認は T1-529。
- 残る既知の Bug は USB LAN の遅さ（BUG-222）。

## 必須

| 項目 | 状態 | LW | 担当 |
| --- | --- | --- | --- |
| [WS083](ws083/ws.md) Vulkan Video（H.264） | p001〜p006・p008 cleared（実機で全 stream 一致、1080p 相当 1 frame 約 5 ms）。2026-10-11 から既定で ON（ユーザー）。p007 の hang の回復は 2026-10-13 ユーザーの実機の確かめ OK（hang の kernel を 1 回置く、手順は phase007、ユーザーの判断） | 0.5 | ユーザー・Q1 |
| [WS202](ws202/ws.md) 動画再生（libmedia の H.264・AAC を Video Player・Music で使う） | p013 の 5330 の UAT OK（2026-10-13 ユーザー）。残り: p012 の T1 の全項目、p014 の規約。スライダーが再生の終わりで約 50% に止まる件は [BUG-300](bugs/BUG-300.md) | 4.0 | T1 |
| [WS197](ws197/ws.md) Bluetooth のスマホ連携（SMS の MAP・通話の HFP・連絡先の PBAP）（2026-10-10 ユーザー「WS197はbeta2.mdで必須に入れておいてください。」） | 2026-10-11: p001〜p005・p011〜p013 cleared。SMS・MMS（文と写真）の受信・表示、PBAP の電話帳・通話の履歴・Settings、Bluetooth の自動復旧は main。残り: p010 の写真・動画の送信（約 3）、HFP の制御 p006（12）と音 p007（25）、実機の UAT p008（Android）。**HFP をベータ2 に入れるかは判断待ち**。規約の p009 はベータ3 | 約 40（HFP を含む。除けば約 6） | 未割当 |
| [WS129](ws129/ws.md) p005・p013 release notes・既知の問題・利用の手引き | 下書き済み（[notes](../docs/release/zedbsd-1.0.0-beta2.md)・[known issues](../docs/release/zedbsd-1.0.0-beta2-known-issues.md)・[guide](../docs/release/zedbsd-1.0.0-beta2-guide.md)）。**ユーザーの review 待ち**。WS199・WS200 の機能を足し、RC で review の comment を消す | 1.5 | ユーザー・P1 |
| [WS129](ws129/ws.md) p006 最終回帰（release の image） | 10/14 | 3 | T1 |
| [WS129](ws129/ws.md) p008 公開の準備（tag・CI・配布物） | 手順は用意済み。10/16、公開はユーザーの指示 | 0.5 | Q1・P1 |
| 次の UAT で出る Bug の枠 | — | 5 | P1 |
| **計** | | **約 55 LW**（約 18 時間、うち WS197 が約 40。HFP をベータ3 に回すと約 18 LW） | |
| [WS225](ws225/ws.md) Vulkan の header を仕様から導ける内容だけで書き直し、Khronos の著作権表示を外す | blocked（2026-10-13 ユーザー「まだ着手せず、ブロッキング」「ベータ2で実装」）。前提の WS224（今の header の解説と表示の形）の後 | 未見積 | 未割当 |

## 次の UAT で確認してほしい事項（5330、WS199・WS200 の後の image）

| # | 項目 | 手順 | 期待 |
| --- | --- | --- | --- |
| 11 | [WS197](ws197/ws.md) スマホの SMS（Android） | 手順は ws197/ws.md の「5330 の UAT の手順」: Settings → Bluetooth でスマホを pairing して「Use as phone」→ Phone の app で受信・送信・同期、app を閉じている間の通知 | SMS が送受信でき、履歴が同期される |
| 12 | [WS197](ws197/ws.md) 写真・動画の MMS の送信と「＋」の添付の選択（2026-10-11、main e7ff23fba 以降の image） | 手順は [phase010](ws197/phase010/phase.md) の末尾: Phone の「＋」→ Photos & Videos で写真・動画を選ぶ（File… でも可）→ 文を添えて Send | スマホから相手に MMS が届く。1 通は元の bytes で合計 8 MiB まで（carrier の上限を越えるとスマホ側で失敗しうる） |
| 13 | [WS197](ws197/ws.md) HFP の通話の制御（音はまだ、p007） | 手順は [phase006](ws197/phase006/phase.md) の末尾: Settings の Bluetooth で「Also use calls」→ スマホに着信 → Phone の通話の card で Answer・経過時間・Keypad の DTMF・Hang Up。Phone を閉じた時の着信の通知。会話の Call で発信 | スマホの通話が PC の操作で応答・終話・発信できる（音はスマホ側のまま） |

## 既知の問題に書いて出す（ベータ3 以降）

| 項目 | 理由 |
| --- | --- |
| BUG-255（蓋を閉じた間の HDMI）、BUG-159（電池で 5 fps）、BUG-145（AX211 の DHCP）、BUG-165（5330 の DSDT） | 調査が長い・実機の時間が要る |
| WS201（/home の暗号化）、WS195（/opt/keiland）、WS196（useradd 等）、WS198（self-build）、[WS001 p045](ws001/phase045/phase.md) POSIX の header、[WS126](ws126/ws.md) Python | ベータ3 の列 |
| 規約の全文の見直しの Phase（各 WS） | ユーザーの決定でベータ3 |

## 終わっていない WS（2026-10-13 Q1、132 件）

2026-10-13 ユーザー「終わっていないWSとその概要、何がブロッキングしているかを、表にして、plan/beta2.mdに入れてください。」→ 各 ws.md の Status・Resume point・Phase の表から作った（記録に無い事は「不明」）。WS は completed でない物（incomplete・planning・planned）。区分は ws.md・この文書・master.md に根拠がある時だけ断定し、master の「ベータ1」の段は fg019 の決定（最初のベータはベータ2）でベータ2 に読み替えた。
注: WS168・WS178・WS187・WS120 は残りの Phase が無く、完了の記録だけが残る。WS172 は ws.md と phase.md の状態が食い違う。WS123 は canceled。

### ベータ2 の物（55 件）

| WS | 概要 | 状態 | 止めている物 | 区分 |
| --- | --- | --- | --- | --- |
| [WS031](ws031/ws.md) | i915 の native Vulkan の実行器と compiler | incomplete。p011 big-bang 待ち、p024・p025 blocked、p021〜p048 の多くが planning | UAT 待ち（p024・p025）、空き時間だけの WS（Focus） | ベータ2（空き時間だけ） |
| [WS033](ws033/ws.md) | 有線 LAN（USB LAN）の常駐管理 | incomplete。p001 in-progress（L2 は p002 へ）、p002 planning、p003 planned | 実機の確かめ（5330、ユーザーの時期）、規約の全文の見直し p003 が未着手 | ベータ2（旧ベータ1 の段） |
| [WS049](ws049/ws.md) | ACPI（SCI・GPE・EC・DSDT）の実機対応 | incomplete。p007・p008・p016 cleared（2026-10-13 ユーザーの実機の確かめ OK、電源ボタン・蓋） | なし（残りの Phase の照合と WS の完了の記録） | ベータ2（旧ベータ1 の段、BUG-165 は既知の問題） |
| [WS050](ws050/ws.md) | USB-C の UCSI（role・Alt Mode） | incomplete。p001〜p005 cleared（p005 は Q1 の判定待ち）、p006 planned | 実機の確かめ（5330、p006）、規約の全文はベータ3 | ベータ2 |
| [WS051](ws051/ws.md) | USB-C の DP Alt Mode（i915 の Type-C） | incomplete。p004c・p005b in-progress、p004a・p004b・p005a は Q1 の判定待ち、p006 planned | 実機の確かめ（5330 の GOP が USB-C の起動・TC1）、BUG-266、規約はベータ3 | ベータ2 |
| [WS075](ws075/ws.md) | i915 の高度化（実行器・compiler・描画の速さ） | incomplete。p006・p007・p032 in-progress、p017〜p031 の一部 uncleared、p010 planning | 描画の高速化はユーザーの再開の指示待ち、空き時間だけの WS（Focus）、p032 は実機 | ベータ2（空き時間だけ） |
| [WS079](ws079/ws.md) | Notes・PDF Viewer | incomplete。実装の残りなし、p017 は Q1 の判定待ち | 実機の確かめ（5330 の mouse・Windows の QEMU の touch で S8・S9、ユーザー） | ベータ2 |
| [WS081](ws081/ws.md) | touch の質（遅れ・報告の率） | incomplete。p007・p017 planned、p009 planning | Windows の機械での touch の計測（ユーザー） | ベータ2（旧ベータ1 の段） |
| [WS083](ws083/ws.md) | Vulkan Video（H.264、Intel Xe-LP） | incomplete。p001〜p008 cleared（p007 は 2026-10-13 ユーザーの実機の確かめ OK）、p009 planning | 規約 p009 はベータ3 | ベータ2 |
| [WS084](ws084/ws.md) | i915 の firmware の画面の引き継ぎ | incomplete。p003 cleared（2026-10-13 ユーザーの実機の確かめ OK、素の起動 10 回）、p004 in-progress | p004 は Q1 の判断待ち（今は何もしない） | ベータ2 |
| [WS089](ws089/ws.md) | Settings の app | incomplete。p011・p014 planned・planning（実機）、p018（規約）planning | 実機の確かめ（5330 の passthrough、p014 は Wi-Fi の driver が要る）、規約はベータ3 | ベータ2 |
| [WS090](ws090/ws.md) | widget の library（libkeiland）と Mahora の font | incomplete。p009・p018 は Q1 の判定待ち、p020・p021 test-wait、p026 in-progress、p012 planning | ユーザーの判断（p020・p021 の PNG の外観）、5330 の UAT と測定（p026）、規約はベータ3 | ベータ2（旧ベータ1 の段） |
| [WS094](ws094/ws.md) | desktop の file の icon | incomplete。実装の残りなし、p007・p009・p012 が残り | 実機の確かめ（5330、p012）、規約はベータ3、p009 はベータ3 の性能の Phase へ | ベータ2（旧ベータ1 の段） |
| [WS099](ws099/ws.md) | compositor（窓の管理・遷移） | incomplete。p012 planned（実機）、p022 planning（規約と回帰）、p006 は記録だけ | 実機の確かめ（5330 の C1 の目視、ユーザー）、規約の全文の見直し p022 が未着手 | ベータ2（旧ベータ1 の段） |
| [WS100](ws100/ws.md) | 音量（audiod と Settings の Sound） | incomplete。p006 cleared（2026-10-13 ユーザーの実機の確かめ OK、HDA の音）、p009（音量の曲線）は QEMU を T1 | T1 の試験（p009） | ベータ2（旧ベータ1 の段） |
| [WS102](ws102/ws.md) | スクリーンキーボード | incomplete。p010・p026 in-progress、p011〜p014 planned | T1 の再試験待ち（p026）と p010 の基準値、p012 は WS095 の続き、p013 は WS085 | ベータ2 |
| [WS113](ws113/ws.md) | 外部・複数 display と Settings の Display | incomplete。p012・p013 in-progress、p014 test-wait、p008・p009・p010・p011a が残り | BUG-266、実機の確かめ（5330 の全経路 p008）、蓋 p011a と規約 p009 はベータ3、p010 は後回し | ベータ2 |
| [WS120](ws120/ws.md) | 音楽の app（m4a と local の collection） | incomplete。p001・p008・p009 cleared、他は canceled、残りの Phase なし | なし（WS の完了の処理が未） | ベータ2 |
| [WS127](ws127/ws.md) | Files の app | incomplete。p007 cleared（2026-10-13 ユーザーの実機の確かめ OK、Files） | FreeBSD の build（p008 の残り、10/13 以降） | ベータ2（旧ベータ1 の段） |
| [WS128](ws128/ws.md) | 標準 app の仕上げ | incomplete。p007（実機）・p012（icon）planning | 実機の確かめ（5330 の通し、ユーザー）、p012 はデザインの方向のユーザーの確認 | ベータ2（旧ベータ1 の段） |
| [WS129](ws129/ws.md) | ベータ2 の release 作業 | incomplete。p002 planned、p005・p013 は下書き済み、p006・p007・p008 planning | ユーザーの review（release notes・既知の問題・手引き）、最終回帰 p006 は 10/14、公開はユーザーの指示 | ベータ2 |
| [WS130](ws130/ws.md) | IPv6 | incomplete。p001〜p007 cleared、p008 は QEMU PASS、p009 planning | 実機の確かめ（5330 の DHCPv6 の UAT は保留）、規約 p009 はベータ3 | ベータ2 |
| [WS131](ws131/ws.md) | libkeiland-backend と libkeiland の分解 | incomplete。p002 in-progress（D7 の確認）、p024 planning | ユーザーの判断 D7 の確認、規約はベータ3、Linux・FreeBSD の回帰は 10/13 以降 | ベータ2 |
| [WS132](ws132/ws.md) | /dev/system の電源・PnP・自動 mount | incomplete。p001 は Q1 の判定、p006・p007・p008 が残り | 実機の確かめ（蓋の事象 p008、UAT p007）、蓋の自動の切り替えはベータ3 | ベータ2（旧ベータ1 の段） |
| [WS134](ws134/ws.md) | System Monitor | incomplete。p003 uncleared、p009・p010 in-progress | T1 の試験（p009）、実機の確かめ（i915 の値・デモ）、規約はベータ3 | ベータ2（旧ベータ1 の段） |
| [WS142](ws142/ws.md) | アプリの切り替え（bar・Alt+Tab・gesture） | incomplete。p001〜p010 cleared | 実機の確かめ（5330 の触った感じと閾値の UAT） | ベータ2 |
| [WS143](ws143/ws.md) | Bluetooth（HID・pairing・Settings） | incomplete。p003・p005 in-progress、p006 test-wait、p007〜p009 planned | T1 の再試験（T1-446・bt-hid-p005）、実機の確かめ（5330 の UAT p008）、Linux は 10/13 以降、規約はベータ3 | ベータ2 |
| [WS145](ws145/ws.md) | 印刷 | incomplete。p001〜p004・p007 cleared、p005・p006 planning | Linux・FreeBSD の guest は 10/13 以降、規約はベータ3 | ベータ2 |
| [WS154](ws154/ws.md) | Languages の頁の IME の選択と SKK の IME | incomplete（p001〜p004 cleared、残りは UAT・範囲の判断・規約） | 実機の確かめ（5330 の SKK の操作感）、ユーザーの判断（`>`・`/`・`#`・Tab・注釈の範囲）、規約の全文はベータ3 | ベータ2 |
| [WS156](ws156/ws.md) | app の通知（下の中央の popup と ring の log） | incomplete（p001〜p004 cleared、p005 uncleared、p006 planning） | 実機の確かめ（5330 の UAT p005）、規約はベータ3、p006（Linux の D-Bus）は後回し | ベータ2 |
| [WS159](ws159/ws.md) | native の touchpad（I2C-HID）と touchpad の層 | incomplete（p001・p003・p004 cleared、p002・p006 in-progress、p005 planned） | 実機の確かめ（5330 の I2C・GPIO の割り込み、p005 の UAT） | ベータ2（表はベータ1、ベータ1 はベータ2 に一本化） |
| [WS161](ws161/ws.md) | YubiKey（USB の FIDO2・NFC の CTAP2） | incomplete。p001〜p005 cleared、p006 は実機の UAT OK（2026-10-13 ユーザーの実機の確かめ OK、YubiKey）、p007 規約 | Linux・FreeBSD の build（p006 の残り）、規約 p007 はベータ3 | ベータ2 |
| [WS164](ws164/ws.md) | 起動時の Welcome の画面 | incomplete（p001・p002 cleared、p003 規約 planned） | 規約の全文の見直し p003（ベータ3）、実機は UAT | ベータ2 |
| [WS165](ws165/ws.md) | 手書きの入力（段 1） | incomplete（p001〜p003・p005 cleared、p004 UAT と規約） | 実機の確かめ（UAT）、規約はベータ3 | ベータ2 |
| [WS168](ws168/ws.md) | プレビューを作る隔離された command | incomplete（p001〜p004 cleared） | 記録に残りの Phase なし（WS の完了の記録が無い） | ベータ2 |
| [WS169](ws169/ws.md) | メーラの app と compositor のメールの API | incomplete（p000〜p005 cleared、p006 は作らない、p007 規約） | 規約 p007 はベータ3（OAuth2 の p006 はユーザーの決定で外した） | ベータ2 |
| [WS170](ws170/ws.md) | Phone の app（連絡先とタイムライン） | incomplete（p000〜p004 cleared、p005 規約） | 規約 p005 はベータ3 | ベータ2 |
| [WS173](ws173/ws.md) | AAT（エージェントが実機を操作する受け入れの枠組み） | incomplete（p001〜p004・p006 cleared、p005 planning） | 素の 5330 の起動の方法（ユーザーの判断） | ベータ2 |
| [WS174](ws174/ws.md) | 起動時の Ctrl・Shift で boot の選択を変える | incomplete（p001〜p003・p005 cleared、p004 BIOS は案） | ユーザーの指示で BIOS（p004）は後日、実機（UEFI の 5330）は UAT | ベータ2（表はベータ1、ベータ1 はベータ2 に一本化） |
| [WS175](ws175/ws.md) | Notes で PDF の画像と文字を編集 | incomplete（p002 判定待ち、p010 in-progress、p011 規約は未作成） | T1 の再実行（p010 の AAT）、p002 は Q1 の判定 | ベータ2 |
| [WS177](ws177/ws.md) | ベータ2 積み残し（準正常系・異常系） | incomplete（p019 以外は cleared） | p019 の残り（Browser の IME (b)(d)）はベータ3 に回した | ベータ2 |
| [WS178](ws178/ws.md) | OpenGL を Desktop へ、GLX を xserver へ | incomplete（p001 cleared） | 記録に残りの Phase なし（x11-p004 の段 3 は BUG-273） | ベータ2 |
| [WS181](ws181/ws.md) | 窓の状態・App Home のモード・整列のモード | incomplete（p001〜p010 cleared、p011・p012 in-progress） | p011 は PNG を見たユーザーの返事待ち、p012 は実機の感触の UAT（T1-559・T1-560 PASS） | ベータ2 |
| [WS182](ws182/ws.md) | 電源ボタンのメニュー（Log Out・Shut Down） | incomplete（p001 cleared、p002 uncleared） | 実機の確かめ（5320・5330 の短押し・長押し） | ベータ2（最後） |
| [WS183](ws183/ws.md) | I2C HID の割り込みと TGL の GPIO の group | incomplete（p002・p003 cleared、p001 test-wait） | 実機の確かめ（5320、電源オフ、ユーザーの時期） | ベータ2 |
| [WS187](ws187/ws.md) | ロック画面（時計・スワイプの解除・認証の選択） | incomplete（p001〜p004 cleared、実機の UAT は 10/10 に確認） | なし（WS の完了の記録待ち） | ベータ2 |
| [WS189](ws189/ws.md) | app の間の drag and drop | incomplete（p001・p004 cleared、p002・p003 test-wait） | T1 の残りの項目（across-displays・pdfviewer の drag-out・photo-to-notes 3〜4 など） | ベータ2 |
| [WS190](ws190/ws.md) | 文字の欄と Text Editor の指での選択 | incomplete（p001〜p003 cleared、p004 規約 planned） | 規約 p004 はベータ3 | ベータ2 |
| [WS191](ws191/ws.md) | 再生の音を libkeiland の audio stream の口へ | incomplete（p002〜p004 cleared、p001 設計は planned のまま、p005 規約） | 規約 p005 はベータ3、p001 の記録が planned のまま | ベータ2 |
| [WS193](ws193/ws.md) | menuconfig の階層の作り直しと Build boot image | incomplete（p001・p002・p004〜p011 cleared、p003 規約 planning） | RPi4 の host key・SSH の確かめと WS の受け入れの照合、規約 p003 が未着手 | ベータ2 |
| [WS194](ws194/ws.md) | keiland-linux・keiland-freebsd の package の確認と導入 | planned と記録（p001・p002 cleared、p003 規約 planning） | 規約の全文の見直し p003 が未着手 | ベータ2 |
| [WS197](ws197/ws.md) | Bluetooth のスマホ連携（SMS・連絡先・通話） | incomplete（p006・p010 in-progress、p007 planning、p008・p014 planned、p009 規約） | ユーザーの判断（HFP をベータ2 に入れるか）、UAT（beta2 の #12・#13）、実機 p008（Android）、規約 p009 はベータ3 | ベータ2（HFP は未決） |
| [WS200](ws200/ws.md) | Users のパスワード変更と認証方式の選択 | incomplete（p001 cleared、p002 planning） | 実機の確かめ（5330 の UAT p002） | ベータ2 |
| [WS202](ws202/ws.md) | libmedia だけで mp4・m4a を再生 | incomplete（p013 UAT OK、p012 planned、p014 planned、他の code の Phase は uncleared） | T1 の全項目（p012）、規約 p014、BUG-300（スライダー） | ベータ2 |
| [WS225](ws225/ws.md) | Vulkan の header を仕様だけで書き直す | planning（blocked、p001 planned） | ユーザーの指示で着手しない（WS226 の後） | ベータ2（blocked） |

### 区分が記録に無い物（今日の Pi 4・網・描画・license などの新しい WS が中心）（25 件）

| WS | 概要 | 状態 | 止めている物 | 区分 |
| --- | --- | --- | --- | --- |
| [WS082](ws082/ws.md) | /dev/kvm の移植の検討 | planning。p001 cleared、以降は未計画 | ユーザーの判断（study.md §10 の 11 項目） | 不明（アイディアの Phase） |
| [WS123](ws123/ws.md) | VA-API | キャンセル（2026-10-02 ユーザー） | なし（キャンセル済み） | 不明 |
| [WS141](ws141/ws.md) | Raspberry Pi 4 の VC6 GPU（表示・V3D・Vulkan） | incomplete。p001〜p006・p013・p014 in-progress、p015〜p019 planning | 実機の確かめ（Pi 4、電源の入れ直しはユーザー）、P3 が担当 | 不明（Target はベータ4 以降、beta2.md は進行中） |
| [WS162](ws162/ws.md) | FIDO2 の login（WS172 に吸収） | incomplete（p001〜p004 canceled、以後実行しない） | なし（WS172 p003 へ移した） | 不明（WS172 に吸収） |
| [WS203](ws203/ws.md) | Pi 4 の内蔵 Ethernet（GENET） | incomplete（p001・p002・p004 cleared、p003 uncleared） | 実機の確かめ（Pi 4 の DHCP の由来・抜き差し） | 不明 |
| [WS204](ws204/ws.md) | HD Audio の録音と HFP 通話の音 | incomplete（p001・p002 in-progress、p003〜p005 planned） | ユーザーの判断 U1・U2 とベータ2 に入れるかの判断 | 不明（判断待ち） |
| [WS205](ws205/ws.md) | Bluetooth audio（A2DP と headset の HFP） | planning（p001〜p003 in-progress、p004〜p011 planned） | ユーザーの判断 D1（試験の headset）とベータ2 に入れるかの判断 | 不明（判断待ち） |
| [WS206](ws206/ws.md) | Pi 4 の WiFi（BCM43455、SDIO） | incomplete（p003 cleared、p001・p002a・p002b・p004・p005・p008 in-progress、p006・p007 planned） | T1-575（p005）実行中、T1 と Pi 4 の実機（p002a・p002b・p004）、ユーザーの判断 E2（試験の AP） | 不明 |
| [WS207](ws207/ws.md) | Pi 4 の音（再生と録音） | planning（p001・p002 in-progress、p003〜p005 planned） | T1 と Pi 4 の実機（p002）、ユーザーの判断 F2（録音の機種） | 不明 |
| [WS208](ws208/ws.md) | arm64 の HAL の SMP（Pi 4 の 4 core） | incomplete（p001〜p004・p007 in-progress、p005・p006 planning） | Pi 4 の実機（p007 の固まりの再現）、T1（p002・p004）、p003 の T1-537 一部 FAIL の Q1 の判定 | 不明 |
| [WS209](ws209/ws.md) | audiod の音の endpoint と route | planning（p001〜p003 in-progress、p004〜p006 planned） | p001 の再 review、call の speaker・mic の QEMU・5330 の確かめ、p004 は WS191 の後 | 不明 |
| [WS210](ws210/ws.md) | Keiland の app の描画を GPU へ | planning（p001・p002・p014 in-progress、p013 planning、p003〜p012 planned） | p001 は Q1 の確認、p002・p013 は Pi 4 の実測 | 不明 |
| [WS211](ws211/ws.md) | Pi 4 の compositor の描画の削減 | planning（p003・p005〜p011 実装済みで Pi 待ち、p001 第 4 版、p002・p004 planned） | Pi 4 の実機の確かめ | 不明 |
| [WS212](ws212/ws.md) | Wiseview の作り直し（仮想デスクトップ・display） | planning（p001〜p005 planned） | 担当が未割当（ユーザーが着手を決める） | 不明 |
| [WS213](ws213/ws.md) | compositor の thread 化（入力・cursor と描画） | planning（p001 第 6 版、p002〜p005・p007 in-progress、p006 planned） | T1 と Pi 4 の確かめ（p002〜p005） | 不明 |
| [WS214](ws214/ws.md) | 窓を HVS の plane に載せる（Pi 4） | planning（p001 第 3 版、p003a・p003b 実装済み、p005・p006 は案、p002 planned） | Pi 4 と T1 の確かめ（p003a・p003b）、p005 は WS213 p004 とユーザーの判断（glass の blur） | 不明 |
| [WS215](ws215/ws.md) | Pi 4 の Bluetooth（BCM43455） | incomplete（p001〜p005 in-progress、p006〜p008 planned） | Pi 4 の実機（p002・p003・p005）、ユーザーの判断 D5 | 不明 |
| [WS216](ws216/ws.md) | Notes の描く道具（筆・鉛筆・塗りつぶし・水彩） | planning（p003 cleared、p004 test-wait、p005 実装中（P1）、p001 review-2 待ち、他は planned） | T1 の試験（画は p007）、p009 は WS141 p017 | 不明 |
| [WS217](ws217/ws.md) | 画面の回転（Settings の Display） | planning（p001 設計中、p002a 実装済み、p002b 以降は案） | T1-576 の (B)（p002a）、p002b 以降は設計の review の後 | 不明 |
| [WS218](ws218/ws.md) | Intel WiFi driver の license の表示を実態に合わせる | planning（p001 planned と記録、p002 in-progress） | T1-573 PASS の Q1 の判定、実機の AX211 の回帰、削除 3 path は Q1 | 不明 |
| [WS219](ws219/ws.md) | Venus の op の流用の整理（出典の明記） | incomplete（p003 cleared、p001・p002 in-progress） | T1-572 PASS の Q1 の判定（p002） | 不明 |
| [WS220](ws220/ws.md) | TCP の転送の速さの安定（受けの window） | incomplete（p001・p002 in-progress、p003 実装済み、p004〜p009 案） | T1-574（基線）実行中、p003 は review と T1 の測り | 不明 |
| [WS221](ws221/ws.md) | Jumbo frame（USB LAN で MTU 9000） | incomplete（p001 第 2 版、p002〜p007 案） | ユーザーの判断 §9 Q1〜Q13、review-2、p005 は WS220 p006・p007 | 不明 |
| [WS223](ws223/ws.md) | 全画面は合成、game mode は拡張の時だけ | planning（p001 第 1 版、p002〜p005 planned） | design-reviewer の後の第 2 版、ユーザーの判断 D1〜D6 | 不明 |
| [WS226](ws226/ws.md) | Vulkan 1.3 の header と libvulkan の 1.3 の実装 | planning（p001 第 1 版） | design-reviewer の review-1、担当は次に空いた P | 不明 |

### ベータ3 以降の物（47 件）

| WS | 概要 | 状態 | 止めている物 | 区分 |
| --- | --- | --- | --- | --- |
| [WS001](ws001/ws.md) | POSIX.1-2024 の utility・header の適合 | incomplete。p041〜p043 in-progress、p044 test-wait、p045 planned、p046〜p051 planning | T1 の guest 試験待ち（p041〜p044）、p045 は 10/17 の後に merge（Q1 の判定）、p046〜p051 は担当が未割当 | ベータ3 以降 |
| [WS004](ws004/ws.md) | ハードウェアの拡張（USB・WLAN・NVMe・PCI） | incomplete。p021・p038 uncleared、p025 planned（実機）、p034 test-wait、p035・p039 planned | 実機の確かめ（5330 の SN740、p025）、T1 の試験（p034）、Queue なし | ベータ3 以降 |
| [WS009](ws009/ws.md) | 製品の文書（GPU の文書ほか） | incomplete。p001〜p008 済み、DOC-54 は Phase 未定義 | GPU の文書の保留の解除待ち（DOC-54） | ベータ3 以降 |
| [WS014](ws014/ws.md) | QEMU の virtio-gpu の GPU・表示の経路 | incomplete。残りは p004（最終 API・規約）planning、旧設計の p001 は手動の保留 | 規約の全文の見直し p004 が未着手（規約はベータ3）、p001 は手動の保留 | ベータ3 以降 |
| [WS026](ws026/ws.md) | 試験の資産の整理 | planning。Phase 未定義 | なし（着手待ち、Phase 未定義） | ベータ3 以降 |
| [WS037](ws037/ws.md) | NVIDIA RTX（nvrtx）の driver | planning。p001 in-progress、p002〜p009 planned・planning | ユーザーの指示で要検討・ブロック、試験機の host の情報待ち（p002） | ベータ3 以降（Target はベータ4 以降） |
| [WS038](ws038/ws.md) | Intel Arc dGPU の driver | planning。番号の予約だけ、Phase なし | ユーザーの指示で要検討・ブロック | ベータ3 以降（Target はベータ4 以降） |
| [WS039](ws039/ws.md) | AMD RDNA GPU の driver | planning。番号の予約だけ、Phase なし | ユーザーの指示で要検討・ブロック | ベータ3 以降（Target はベータ4 以降） |
| [WS044](ws044/ws.md) | Raspberry Pi 4 の開発環境（font・FAT32・lldb） | planning。p003（lldb）・p004（規約）planning | ユーザーの指示で要検討・ブロック、実機の結果待ち（p001・p005〜p009） | ベータ3 以降（Target はベータ4 以降） |
| [WS046](ws046/ws.md) | GNU 互換の make | incomplete。p011・p014 uncleared、p005（規約と回帰）planning | なし（着手待ち、p014 の当て直しから、Queue なし） | ベータ3 以降 |
| [WS048](ws048/ws.md) | Raspberry Pi 4 の USB（hub・HID キーボード） | incomplete。p015 の直しは main に統合済み、p006・p007 planned | 実機の確かめ（Pi 4、ユーザー: 通常の cancel log の停止と USB 入力） | ベータ3 以降（Target はベータ4 以降） |
| [WS052](ws052/ws.md) | 電源管理（S0i3 の sleep） | incomplete。p001・p002・p005・p006・p009 in-progress、p010〜p012 test-wait、p007 planning、p008 planned | T1 の QEMU の試験と実機の確かめ（5330 の UAT）、設計の review（p001・p007） | ベータ3 以降 |
| [WS068](ws068/ws.md) | EGL・OpenGL ES・desktop GL | incomplete。p004・p009 in-progress、p007・p039 ほか planning・planned、GL 3.3 以降（p034〜p037）は保留 | GL 3.3 以降はユーザーの指示で保留、p009 は T1-180 の計測待ち | ベータ3 以降 |
| [WS074](ws074/ws.md) | Web ブラウザ（libbrowser と shell） | incomplete。p100 ほか多数が planned、待機 | ユーザーの指示で保留（レンダリングの改善は指示まで行わない） | ベータ3 以降 |
| [WS080](ws080/ws.md) | PE/COFF の動的ローダ ld.coff | planning。p001 の設計まで、p002〜p012 planning | ユーザーの指示で要検討・ブロック、ユーザーの判断 D1〜D17 と HAL の差分の承認（A1・A2） | ベータ3 以降（Target はベータ4 以降） |
| [WS095](ws095/ws.md) | 日本語 IME | incomplete。p001〜p005・p012 cleared、p006・p007 は T1 待ち、p009〜p011 planning | T1 の試験待ち（p006・p007）、p009 は WS074 の担当次第 | ベータ3 以降（続き） |
| [WS101](ws101/ws.md) | GPU の compute（Noct の自動並列化） | incomplete。L1 で区切り、p012・p013・p018 planned、p016・p017 は優先を下げた | ユーザーの指示で最適化の優先を下げた（再開の指示待ち） | ベータ3 以降 |
| [WS106](ws106/ws.md) | test app を userland/tests へ集約 | incomplete。p002 は cleared 候補、p003 planning | なし（着手待ち） | ベータ3 以降 |
| [WS112](ws112/ws.md) | Debian・Ubuntu・RPi OS の deb | incomplete。p002・p003 cleared、p007（規約）planned | 規約の全文の見直し p007 が未着手 | ベータ3 以降（残りは規約） |
| [WS115](ws115/ws.md) | upstream GTK4 の移植 | planning。p010 in-progress（中断）、p003 planning | rtld の上限で GTK が起動しない（p010 の再開点） | ベータ3 以降 |
| [WS116](ws116/ws.md) | upstream Qt6 の移植 | planning。p001〜p005 planning | 他の WS の依存（WS117 p002・WS115 p008） | ベータ3 以降 |
| [WS117](ws117/ws.md) | Linux の標準 Qt6 の互換 | planned。p001 planned、p002〜p006 planning | ユーザーの判断（p002 の行別の採否）、p001 は WS114 p007 の後 | ベータ3 以降 |
| [WS118](ws118/ws.md) | Latitude 5320 の対応 | incomplete。p006・p007 cleared、p005 uncleared、p001〜p004 planning | 実機の確かめ（5320、電源オフ）、ユーザーの立会い | ベータ3 以降（Target はベータ4 以降） |
| [WS119](ws119/ws.md) | インストーラの作り直し | planning。p001 planned、p002〜p006 planning | アイディアの Phase の後のユーザーとの議論と要件の判断 | ベータ3 以降（Target はベータ4 以降） |
| [WS121](ws121/ws.md) | browser の動画の再生 | incomplete。p001・p002・p004〜p006 cleared、p003（Range）planning | browser（WS074）と一緒にベータ3 へ（Q1 の判定） | ベータ3 以降 |
| [WS124](ws124/ws.md) | Emacs（端末版）の package | planning。p001・p002 planned、p003〜p006 planning | 担当が未割当、p004 は ws125-p002 の後 | ベータ3 以降（Target はベータ4 以降） |
| [WS125](ws125/ws.md) | vim の package | planning。p002 test-wait、p001 planned、p003・p004 planning | T1 の試験（ws126-p005 の image の boot） | ベータ3 以降（Target はベータ4 以降） |
| [WS126](ws126/ws.md) | Python 3（core）の package | planning。p002・p003 cleared、p005 test-wait、p004 は行わない、p006 planning | T1 の試験（p005） | ベータ3 以降 |
| [WS139](ws139/ws.md) | desktop の速さ | incomplete。p001 in-progress、p002・p003 planned | T1 の計測（E1）、ユーザーの判断 U2（5330 の操作）・U1・U3・U4 | ベータ3 以降 |
| [WS144](ws144/ws.md) | VPN | planning。p001 planning | アイディアの Phase の後のユーザーとの議論 | ベータ3 以降（Target はベータ4 以降） |
| [WS146](ws146/ws.md) | SSH の独自のオンラインストレージ | planning。p001 planning | 時期の決定、WS150 の後 | ベータ3 以降（Target はベータ4 以降） |
| [WS147](ws147/ws.md) | OneDrive の連携 | planning。p001 planning | 時期の決定、WS150 の後 | ベータ3 以降（Target はベータ4 以降） |
| [WS152](ws152/ws.md) | system の更新（Settings の Updates の頁の実体） | planning（p001 方式の検討の第 2 版） | ユーザーの判断 U1〜U12 | ベータ3 以降 |
| [WS155](ws155/ws.md) | カレンダーの app（予定・メモ） | incomplete（p000〜p004 cleared、p006 in-progress、p005 規約） | 実機の確かめ（p006 の UAT、T1-561 は PASS）、規約 p005 はベータ3 | ベータ3 以降（続き。p006 の区分は不明） |
| [WS157](ws157/ws.md) | 写真の管理の app（Photos） | incomplete（p011〜p014・p016 in-progress、p015・p017 planned） | T1 の試験待ち（p013・p014・p016）、p016 は main に未統合、実機の GUI の UAT は未実施 | ベータ3 以降（続き。ベータ2 の範囲は済み） |
| [WS158](ws158/ws.md) | Keiland と app の翻訳（日本語） | incomplete（p001 cleared、p002〜p005 in-progress・test-wait、p006 planning） | T1 の試験（p003・p004）、ユーザーの日本語の review（p005） | ベータ3 以降 |
| [WS171](ws171/ws.md) | hal.h の全関数に契約の comment | planning（p001 cleared、p002 差分の案・p003 適用） | 担当が未割当、p002 はユーザーの review が要る | ベータ3 以降 |
| [WS172](ws172/ws.md) | passkey の認証の枠組み（/sbin/passkey） | incomplete（p002・p003 cleared、p007 test-wait、p004〜p006 planning） | T1 の試験（p007 の再依頼）、p001 は phase.md で判断 P1〜P10 待ちのまま（ws.md は cleared と記録） | ベータ3 以降 |
| [WS176](ws176/ws.md) | Canvas（pen のイラストと画像の編集） | planning（p001 planning） | ユーザーの決定でベータ3 | ベータ3 以降 |
| [WS180](ws180/ws.md) | Emacs ベースのグラフィカルなエディタ | planning（Phase 未定） | ユーザーの決定でベータ3 以降（WS だけ作成） | ベータ3 以降 |
| [WS184](ws184/ws.md) | 左手デバイスの OSK（クリエイターモード） | planning（p001・p002 planned） | ユーザーの決定でベータ3 | ベータ3 以降 |
| [WS185](ws185/ws.md) | ゲームパッドの OSK とゲームコンソールモード | planning（p001〜p003 planned） | ユーザーの決定でベータ3 | ベータ3 以降 |
| [WS186](ws186/ws.md) | Realtek RTL8822CE の WiFi（5320） | planning（p001 planned） | ユーザーの決定でベータ3（ベータ2 が早く終われば前倒し） | ベータ3 以降 |
| [WS195](ws195/ws.md) | userland/desktop を /opt/keiland/ へ | planning（p001 設計） | ユーザーの判断 D1〜D7、design-reviewer | ベータ3 以降 |
| [WS196](ws196/ws.md) | useradd・usermod・userdel | planning（p001 範囲の設計） | ユーザーの判断 E1〜E6、design-reviewer、WS195 p001 | ベータ3 以降 |
| [WS198](ws198/ws.md) | zedBSD の上で zedBSD を self-build | planning（p001 cleared 候補） | ユーザーの決定でベータ3、p001 は Q1 の判定 | ベータ3 以降 |
| [WS201](ws201/ws.md) | /home の暗号化（FIDO2 の PRF と key slot） | planning（p001 設計） | ユーザーの指示で今は検討しない | ベータ3 以降 |

### 保留（ユーザーの指示・判断待ち・Future Work）（5 件）

| WS | 概要 | 状態 | 止めている物 | 区分 |
| --- | --- | --- | --- | --- |
| [WS013](ws013/ws.md) | CPAR（kernel・root・app を file で選ぶ区画） | incomplete。boot の基盤 p002〜p006 済み、残りは F-002 | Future Work F-002 に保留（昇格まで再開しない） | 保留 |
| [WS098](ws098/ws.md) | IME のニューラル化 | planning。p001（設計）planning | ユーザーの指示で要検討・ブロック、WS095 の基本の辞書 | 保留（要検討・ブロック） |
| [WS150](ws150/ws.md) | FUSE に当たる枠組み | planning。p001 planning | ユーザーの指示で要検討・ブロック | 保留（要検討・ブロック） |
| [WS153](ws153/ws.md) | Settings の Apps の頁と third-party の app の repository | planning（p001 設計の第 4.1 版、p002 以降は未定） | ユーザーの判断 U2〜U15（ユーザーが検討中、Q1 は聞かない） | 保留 |
| [WS222](ws222/ws.md) | 画像・PDF・字形の library を Desktop へ戻す | planned（p001 planned） | ユーザーの指示で今の作業が全部終わってから | 保留 |

## 運用

- 凍結の目標の 10/13 の後も、ベータなので UAT の Bug の直しは 10/16 の準備に間に合う範囲で続ける。新しい仕様の変更は「ベータ3 に回すか」を Q1 がユーザーに聞く。
- Q1 は進むたびにこの表を更新し、完了した項目を消す（2026-10-09 ユーザー「都度、beta2.mdを更新していただけると、進捗がわかって助かります」）。
