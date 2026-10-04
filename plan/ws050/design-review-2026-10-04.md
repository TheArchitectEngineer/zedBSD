# WS050 design.md の敵対的レビュー（2026-10-04、P1 generation15 が起動した design-reviewer の報告、Q1 が受領して転記）

対象: /home/awe/zedBSD-worktrees/p1/plan/ws050/design.md（generation15 の草案）。file は変更していない。5330 の ssdt8・dsdt を iasl -d で逆アセンブルした物は /tmp/claude-1000/-home-awe-zedBSD-claude1/ab9a858a-8d77-5248-b279-6b6d7966d812/scratchpad/dr/。UCSI の仕様書は手元に無く、【仕様要確認】は記憶による。

## A. 確定した誤りと危険（code・AML で確認）

- A1 [高] §3・§4・§6: mailbox の読みが _Q79 の書き込みと競合しちぎれた値を読む。_Q79 は event thread（acpi-ec.c:478-517）で ECMU の下で MGI0..F・CCI0..3 を 1 byte ずつ書き、field は `Field (USBC, ByteAcc, Lock, Preserve)` で byte ごとに Global Lock、取り合いで drv_acpi_sleep が interpreter lock を放す（aml-sync.c、aml-thread.c:130-146）。driver の直接の読みは守られない。修正案: WS049 に「1 つの interpreter entry の中で callback を走らせる」公開 API（drv_acpi_enter/leave の公開版）、_DSM 2 と mailbox の写しを同じ entry で、CCI→MESSAGE_IN→CCI の二度読み、_Q79 が割り込む host 試験。
- A2 [高] §5: VERSION の確認が手順の最後。VER1/VER2 を書くのは _STA だけ（EC 0x80/0x81）。手順を「_STA → VERSION → 判定」にし PPM_RESET の前へ。attach は EC の _REG（ECRD=1）の後と明記（ECRD=0 だと _STA が SMI の経路、_Q79 は Notify せず返る）。boot での位置（pcat.c:233 の drv_acpi_attach の後）を決める。
- A3 [中] §3: 「mailbox の memory は GNVS」は誤り。UBCB は GNVS の 32 bit の field（dsdt.dsl:2472、GNVS は 0x6150C000 から 0xCE0）で中身は pointer。mailbox の memory の種類は不明。page.c は BOOT_RECLAIM も allocator に入れる（WS049 design §1.4）ので、boot services data なら kernel が再利用して壊れる。UAT で UBCB の値と memory map の型を記録、USABLE・BOOT_RECLAIM なら attach しない（memory map を問う口の要否は未確認）。
- A4 [中] §3: 「driver も同じ写像で」はできない。AML の写像は private な LRU の cache（acpi-kern.c:820-870）で追い出しで unmap。driver は HAL_SPACE_READ|WRITE|NOCACHE で別に写像（属性は AML と同じ）、page の不揃い・跨ぎを扱う。
- A5 [中] §4・§6: Notify handler は Notify opcode から同期で呼ばれ（aml-operator.c:1880 → aml-sync.c:91-110）、interpreter lock と ECMU を持った状態、走る thread は AML を走らせている thread（通常は event thread だが限らない）。lock 順を明記: state mutex を持ったまま drv_acpi_evaluate を呼ばない、handler は state mutex を取らず spinlock を最内にして waitq を起こすだけ、spinlock・waitq は notify_install の前に初期化。kern/lock.h の rank の検査は lock.c に無い（文書上の順でよい）。
- A6 [中] §6: driver を止めた後も Notify handler が残る（acpi.h に uninstall が無い、aml-sync.c:62）。drv_acpi_notify_install は lock 無しで node->notify を書き換え、event thread の配送と競合。softc は解放せず stopped の flag、install は interpreter entry の中か WS049 側で直す。
- A7 [中] §8: drv_acpi_object_package_new は aml-internal.h:532 の内部 API で acpi.h に無い（_DSM の Arg3 の空の package を作れない）。function 0 の戻りは Buffer(1){0x1F} で drv_acpi_evaluate_integer は使えない。ToUUID の混合 endian を誤ると function 1・2 は黙って Buffer{0}。§5-3 の function 0 の bit 1・2 の検査は必須として残す。package の生成の公開を WS049 への依存に。
- A8 [中] §6: CCI の 10ms の poll は interpreter と CPU を独占（_DSM 2 は 1 回で \ECRB を 20 回、Serialized の ECR1・120 段の If・EC の transaction、待ちは interpreter lock を持った busy stall（acpi-ec.c:655）、Global Lock 付きの書き込み 20 回）。Notify の後は _Q79 が写し終えているので _DSM 2 を呼ばず直接読む、poll は Notify が来ない時だけ 20ms→100ms と延ばす、_DSM 1・2 の所要を実機で測る。
- A9 [中] §2.1・§3: connector の番号と CR01〜CR0A の対応が未定。CR0x は全部 _ADR 0、TTUP/TPxU/ITCP の条件付き（番号が飛ぶ）。「CR0n = connector n」を実機で確かめ、規則と合わない時の扱いを書く。

## B. 仕様との不一致の疑い【仕様要確認】

- B1 [高] 向きは UCSI 1.x の GET_CONNECTOR_STATUS に無い（2.0 で追加、記憶）。ws.md の受け入れと §1・§4 の「向き」を見直す（2.0 以上だけか、TCSS 側の出所（未確認））。
- B2 [高] §2.1・§10-3: 配置を VERSION で決めると 5330 で止まりうる（VERSION は EC から実行時、未知）。配置は AML の region（0x38、MGI・MGO 16 byte）で決め、VERSION は field の意味の判定に。2.x で MESSAGE_IN が大きくなる版も要確認。p002 の前に 5330 の VERSION を実機で得る。
- B3 [中] §2.4: ACK の規則の抜け（ACK_CC_CI の完了は CCI の Acknowledge Command bit、次の command は完了を待つ、ACK 自体は ACK しない、error の完了も ACK、busy・ACK 待ちの間の CCI の connector 番号も latch、完了と connector change は 1 回の ACK でまとめてよい、CANCEL が表に無い）。
- B4 [中] §5: GET_CURRENT_CAM・GET_CAM_SUPPORTED の offset は Recipient が connector の GET_ALTERNATE_MODES の一覧への index。§5 は SOP の一覧しか読まない。MESSAGE_IN 16 byte に alt mode は 2 件、GET_PDOS は 4 件まで → offset を進めて繰り返す。
- B5 [中] §4・§8: DP の pin の割り当てと HPD は UCSI 1.x で得られない（MID は能力の VDO）。WS051 ws.md（pin・HPD は UCSI を通して）と WS050 ws.md（HPD の通知）が §8（HPD は i915）と食い違う。§8 に「得られるのは mode に入った事実と能力の VDO だけ」、pin と lane は i915 の TCSS/FIA から（WS051 p001 に確認を渡す）。
- B6 [中] §5: SET_NOTIFICATION_ENABLE を列挙・GET_CAPABILITY の前に全種類で出している。最初は command の完了と error だけ、列挙の後に bmOptionalFeatures で絞って残りを有効にし全 connector を読み直す。
- B7 [中] PPM_RESET: reset 後は通知が無効で完了は poll だけ。EC の CCI に前の Reset Completed が残り偽の完了に見える恐れ（推測）。回復の reset は全状態を無効にし、利用者に「不明」と新しい generation を知らせて列挙し直す。
- B8 [低] power operation mode に BC が抜け、1.x の GET_CONNECTOR_CAPABILITY に「USB の世代」は無い、_DSM の Arg1（revision）の値が未記載。

## C. 抜けている失敗の扱い

- C1 [中] suspend・resume（WS052 の S0ix）: resume 後の通知の再有効化か PPM_RESET、suspend 中の _Q79 の扱い。WS052 への依存として書く。
- C2 [低] 利用者の callback は ucsi thread から呼ばれる。callback が ucsi thread を待つ要求を出すと deadlock。「callback は block しない、要求は queue に積む」。
- C3 [低] /dev/typec の cdev 番号と名前を UAPI を足さない方針でどう扱うか。

## D. 試験計画の穴

- 疑似の PPM は設計と同じ読み方なので B1・B4・B5 を検出できない。実機の mailbox の記録（CONTROL・CCI・MESSAGE_IN の列）を host の fixture にして再生、同じ 5330 の Linux の /sys/class/typec を black box の正解値に（code は写さない）。
- AML の harness: EC の模擬は ECRD=1 に（でないと SMI の経路で _Q79 が Notify しない）。_STA が 0x0F になるには \_SB.PC00._INI（dsdt.dsl:28042）で OSYS を設定。_Q79 と ucsi thread が交互に走る試験が無い。
- QEMU の boot test は T1 に依頼と明記。p005 の機材（DP の monitor、USB-C→DP、PD の充電器、hub）と正解値の出所。

## E. Phase の分割と依存

- p002 の依存は §10-1 でなく §10-3 と実機の VERSION（B2）。
- p002・p003・p004 の分け方が §4 と合わない（状態の保持・診断は typec.c（p004）なのに /dev/typec が p003、typec.h は p002 の host 試験で要る、Alternate Mode・PDO の読み直しは核なのに p004）。
- WS049 に足す API（公開の package の生成、notify の除去と同期、interpreter entry の中で callback を走らせる口、_CRS を解く共通の関数）が計画に無い。WS049 に Phase を足すか p009 の前に入れるよう Q1 が調整。
- ws.md の目標の role の切替・Alternate Mode の選択は §10-2（入れない）が採用されたら直す（HAL の通知を含む範囲も）。

## F. 確かめて問題が無かった点

§3 の _STA の条件（USTC、EC 0x80/0x81、OSYS ≥ 0x07DF）、_CRS の Memory32Fixed（基底 UBCB、長さ 0x1000）、region の 0x38 byte と field の並び、_DSM の function 0 = 0x1F・1 = write（MGO→EC 0xA0..0xAF、CTL→0x88..0x8F、EC 0xB0 に 0xE0）・2 = read（0x90..0x9F と 0x84..0x87）、別 UUID の function 5、_Q79 の写しと Notify (0x80)（ECMU の中）、_PLD の GPOS の bit の位置（xHCI 側の ssdt7・10・12 も同じ）。HAL の変更は不要（hal_space_map_device がある）、要るのは WS049 の driver API の追加。
