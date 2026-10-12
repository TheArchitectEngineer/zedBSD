# WS215 p001 第 2 版 review-2（design-reviewer、2026-10-13）— 要点

判定: p002 は進めてよい。p003 は条件つき（中-A を Q1 が解く、中-B〜E を p003 の節に数行ずつ）。p005 は p003・p004 の後で中-B・中-F の反映が条件。致命・重大の新しい指摘なし。重大 1〜3 と中-1 は骨格として解決。

- 中-A: ws206-p002a が WS206 の記録に無い（review の時点）。→ Q1: WS206 p001 第 3 版（main 3db8fa90f）で p002a（GPIO・mailbox の GPIO の helper だけ、依存は p001 の review）を立て、pull は列挙型 `enum drv_rpi4_gpio_pull`、mailbox の line は `enum drv_rpi4_firmware_gpio`（BT_ON=0・WL_ON=1、128+line の変換は helper の中）に決めた。WS215 は WS206 の W3 の API を引用する。
- 中-B: daemon の始めの BT_IOC_RESET が STOPPED を毎回消し「60 秒に 3 回」が働かない（main.c:794-795、988-999）、「bt の RECOVER の reset で戻る」は誤り（session が閉じていると `ERROR no-controller`、main.c:1841-1845、戻せるのは reopen）→ (i) BT_IOC_RESET は STOPPED を出るが 60 秒の窓の履歴は消さない、(ii) daemon が READY から T 秒以内の求めていない notice を BTD_FAILURES_MAX に数え RECOVER で 0、文を「RECOVER の reopen」に、host 試験に「周期的な notice で daemon が止まる」。
- 中-C: HUNT が長い偽の候補で本物の Command Complete を飲む（0x04 0x0E 0xFF が 255 byte を待つ）→ HUNT では header の byte ごとに判定（type 0x04、code 0x0E か 0x0F、0x0F は len==4、0x0E は len≥4）、不一致・時間切れは候補の始まり +1 から保持した byte を replay、host 試験に「偽の header の prefix + 本物の CC」、replay の buffer を設計に。
- 中-D: reset の op と worker の自己 reset の取り合い → 電源の世代番号、worker は止まりの合図を mutex の外で、mutex を取った後に世代が変われば自己 reset を取り消す、class に断られて持つ packet は reset で捨てる。
- 中-E: 登録の flags（bt-hci.c:226-228 は sco の op がある時だけ flags を設定）のままだと BT_INFO_SPEED・ADDRESS が消える → flag ごとに条件を分ける（SCO は sco の op、SPEED は set_speed != NULL、ADDRESS は description の値）と B20 に。
- 中-F: D5 の症状の記述に出典が無い → p006 の 10 分の試験で送りの止まりに加えて PL011 の割り込みの数と率も記録。
- 軽微: notice の取り違え（ioctl の後 EAGAIN まで非 block で読み捨て）、Broadcom の folder を session の欄に（host 試験の差し込み）、RECOVER で電源の入れ直しが 2 回（記録だけ）、firmware の file の名前を Read Local Name の生の文字列から作らない（表か文字の制限、製造者 15 と Cypress 305）、古い記述（emulated_board → drv_rpi4_board_emulated、SMP の不安全は済み）、vmunix.mk の行番号は 590・598・605、mailbox の GPIO の答えの状態の語（helper が確かめる）、p002 で Pi の既定の image に bluetoothd を入れるかを明記（計測の時期）。
- 値の出典の表: 形は妥当。p003・p005 の入る時に Zephyr・Android の file の path・commit・行を、自前の値（10ms・100ms・1s・2s）は「自前の決め」と明記。
- D1 賛成、D2 条件つき賛成、D3 賛成、D4 賛成、D5 (a) は中-F の観察と中-B の先の直しが条件、(b) の前に ARM の公開の PL011 の errata を探す (c) を足す。
