# ws143-p001: design.md 第 2 版の design-reviewer の指摘（2026-10-05、agent a40b549b4e89b1245）

F1〜F25 の状態（第 2 版）: 解決 F1・F2・F6・F9・F10・F13・F15・F17・F20・F21・F22・F24・F25、一部 F3・F4・F5・F7・F8・F11・F12・F14・
F16・F18・F19・F23。§9 は送る前に直すこと（N1〜N6・N9）。第 3 版で全て反映した（各節の `[Nn]`）。

| # | 重さ | 節 | 指摘（要旨） |
| --- | --- | --- | --- |
| N1 | major | §5.1・§5.3・§6.5 | root だけが開ける node と「開けた後に特権を落とす」と「re-enumerate の後に新しい node を開け直す」が矛盾。特権の親と SCM_RIGHTS / devfs の持ち主 / root で起こし直す、を D16 に |
| N2 | major | §5.1・§3 | `drv_usb_device_reset()`（usb.c:1252-1570）はその場の reset で node は消えない、root port 以外 ENOTSUP、URB があれば EBUSY、callback の文脈で呼ばない、失敗で隔離。「USB の reset で bootloader に戻して 1 回やり直す」は出典が無い（FreeBSD は成功の後に reset、main.c:818-824） |
| N3 | major | D13・§10.3 | guest が Intel Reset で bootloader に戻す方法は licence の通る出典が無い。推奨を host の blacklist に |
| N4 | major | §6.2・§6.4 | 鍵の長さの検査（KNOB、CVE-2019-9506）、BLUFFS の類。D10 に SC Only |
| N5 | major | §6.1・§6.4 | BR/EDR の Link Key Request の Reply・Negative Reply、PIN Code Request、Authentication_Requested、暗号化、SSP・SC の host の設定、scan と CoD、HIDP の control の message、再接続の向きは SDP の HIDReconnectInitiate・HIDNormallyConnectable |
| N6 | minor | §8.1 | KL_VERSION は 33（keiland.h:49）、manager の版は既に 9（kl-system-protocol.h:191、318 行が使う）なので 10 |
| N7 | minor | §3 | 断片は 252 byte が溜まるたび（command の境で分かれてよい）、variant 0x17 以上の本体は 964 byte から、CSS の版の検査、断片の応答の数え方 |
| N8 | minor〜major | §6.5・D8 | seat の人は volumed の先例（`/dev/gpu0` の持ち主、`_greeter` を除く）、agent の名乗りは SO_PEERCRED の uid で、SSH は区別できない、greeter での pairing |
| N9 | major | §9 | D15 を先に、D4 に本物の選択肢、D11 を分ける、D12 は情報のお願い、D6 は読んだ id に合う block、D10 の危険の明記、D5 の代わりの承認、UAT の環境、専用の account、D16 の残る危険 |
| N10 | minor | §6.6 | b1 の status の検査、b2 の invalid curve（CVE-2018-5383）、libpdf の AES |
| N11 | minor | §5.3 | USB の event は node を言わない（scan し直す、OVERFLOW）、resume の印が無い（0x80 を出して Read Version をやり直し、古い report を捨てる） |
| N12 | minor | §5.1 | event と ACL の別の上限、bulk の組み直しの回復、lock の外で copy |
| N13 | minor | §5.1・§10.2 | firmware の要らない dongle はそのまま使う、dongle と host の BlueZ で UAT の前に相互の接続を試す |
| N14 | minor〜major | §10.3・p008 | UAT が素の機械か passthrough か（共存の確認は素の機械だけ）、受け入れ 3 に suspend |
| N15 | minor | §6.5 | socket は `/run/bluetoothd.sock`（networkd・volumed・audiod と同じ） |
| N16 | minor | §5.2 | read は EAGAIN、名前を 63 byte に UTF-8 の境で、usb-hid の code は規約の全文に |
| N17 | minor | 出典 | FreeBSD の commit の hash |
