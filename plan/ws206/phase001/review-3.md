# WS206 p001 第 3 版 review-3（design-reviewer、2026-10-13、main 9fb4ba466）— 要点

判定: review-2 の F1・S1〜S4 に形の上で全部対応、致命なし。中 7 つが残る。
- p002a: 進めてよい。条件: kern_platform_init から drv_rpi4_firmware_init を呼ぶ（8(b)）、rpi4-gpio.c を rpi4 で常に build（8(c)）、mailbox の GPIO の向きを読む helper（8(a)）。
- p002b: 条件付き可。BUG-295 への依存は外せる（指摘 7）ので外すのを推す。指摘 2・3 を W12・W8 に。
- p008: 可。条件は指摘 5・6 と軽微 1 を W6 に。p005 の前に指摘 1、p004 の前に指摘 4。

1 中: EACCES を SoftMAC と同じ規則に（SoftMAC は M2 の後の deauth・disassoc だけ wlan.c:950-967、M3 が来ない時間切れは ETIMEDOUT wlan-wpa2.c:552-554、networkd は EACCES で profile を二度と試さない main.c:4659・8136）→ EACCES は WAIT_M3 で DEAUTH_IND・DISASSOC_IND と reason 15 だけ、M3 の timeout は ETIMEDOUT、AUTHORIZED の前で sup の状態が不明の deauth は ECONNRESET。EPROTO は /sbin/wifi の再試行の対象外（wifi/main.c:1812-1824）→ 再試行なら ECONNRESET か、終わらせると明記。host 試験に。
2 中（SMP）: control の mutex と bus の thread の deadlock（barrier が bus を待ち、bus が slot のために mutex）→ bus の thread は control の mutex も lifecycle の mutex も取らない（slot の受け渡しは ring の spinlock）、または barrier は mutex を放して待つ。
3 中: shutdown との直列（mutex を持ったまま待つと publish の手順 7 と deadlock、net の shutdown は driver を通さず net_device_gone を呼ぶ core.c:285-330・net-device.c:391-427、wlan.h:166-172 の約束）→ stopping を立てて mutex を放してから待つ、口が無ければ「registry が止まった後の net_device_create の失敗と attach の ENODEV を正常な終わり」と決め host 試験に、shutdown の口の要否を p004 の前に Q1 へ。
4 中: PMK の写しが受けの側（WSEC_PMK の応答の F2 の受けの buffer、control の応答の slot、bounce buffer）に残る恐れ（推測）→ 消す対象に足し偽の bus の試験で。
5 中（SMP）: report_ethernet が engine の状態を lock なしで読む → station の spinlock の下で controlled_port と connection_generation だけを見るか control gate。
6 中: attach の ops の検査（wlan.c:299-308）を mode で分ける時に書き換えると明記、management_transmit が NULL で ACTIVE_ALLOWED は EINVAL（:310-316、:445-455）→ channel は OFFLOADED_DWELL だけ、escan の active・passive は driver の私的な表（AX211 も intel-ax211.c:4065）。
7 中: p002b の BUG-295 への依存は外せる（SD の base は src/kern/platform/rpi4.c:82-91 で static に記録して返す、rpi4-sdhci.c に触れない、QEMU は drv_rpi4_board_emulated、fdt_phys も同じ所で保存 rpi4.c:157-172）。
8 軽微: link loss の reason（BUG-157）を wlan_fullmac_engine_fail に、W12 の「lock を持ったまま core を呼ばない」と W8 の手順 7 の矛盾は例外として（AX211 も intel-ax211.c:2463-2626）、EBUSY の 50ms の再試行は claim_buffer が既に 2〜4 秒待つので効かない（rpi4-firmware.c:349-365）、WS215 への補い (a) GPIO の向きを読む helper (b) kern_platform_init で drv_rpi4_firmware_init（今は rpi4-pcie.c:182 だけ）(c) rpi4-gpio.c を常に build、W8 の順は root を待って file を確かめてから hardware、escan の隣の channel の ESTALE は正常（wlan.c:578-588）・最後の complete の後の scan_stop（:5384-5390）・probe response 2304 byte 超は IE を切る、lost の後の同じ generation の associated は無視、plan/ws005/tests/host-wlan-retire.sh は master の Tools に無い（回帰に使うなら登録を Q1 に）、lock の rank は全部 LOCK_RANK_DEVICE の入れ子で順は表の文で守る。
問いの文: E2 に「その AP の暗号は WPA2（AES）か、WPA3 だけ・PMF 必須なら v1 は繋がらない」を足す。E4 は「ほぼ同じ中身（2 行だけ違う）」、(1) に「いいえ」の時の案、(3) firmware 本体と CLM の Cypress の EULA（Cypress の IC と共に、改変の禁止）を Pi 4 の image で配ってよいかを足す。E6 は「NVRAM と firmware の既定（p004 で実際の国の値を読んで記録）」に。
確かめた点: S1 の 22 行の表は wlan.c の WPA2 の参照 146 行を関数の単位で全部覆う、S2 の順、S3 の latch、F1 の dispatcher、WS215 の 3 つの要求、HAL・UAPI 変更なし。
