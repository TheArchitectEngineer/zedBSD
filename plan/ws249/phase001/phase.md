<!-- awesome-plan project=zedbsd record=ws249-p001 -->

# ws249-p001: BCM43455 の driver の出典の表記（block ごとの referenced:）

Status: cleared（2026-10-15、q1205-i01、P2。実機の確かめは WS の最後に T へ）
Parent: [WS249](../ws.md)
Approval: 2026-10-15 ユーザー「bcm43455も同じガードレールで修正してほしいです。WSを立ててください。」（WS の作成。実行は担当の割り当ての時に Queue に入れる）

## 範囲

ws.md の「完了の条件」のうち、この Phase の分。動作は変えない。手順は WS248 の同じ Phase に倣う。

追加の範囲（2026-10-15、Q1 経由のユーザーの決定、原文）:「開発管理はすべてのソースから削除しますが、今は作業対象になったときに削除するだけでいいです。…WS249では削除を行いましょう。」
→ bcm43455 の comment・log から開発の管理情報（WS・Phase の番号、qNNN、BUG-NNN、T の試験の回、plan/ への参照、設計の項目の番号、日付つきの決定の記録）を削除する。log の文字列が変わったら、それに合わせる plan/ の試験・道具を直して一覧にする。

## 結果（2026-10-15、P2）

- 変更: `src/drivers/wifi/bcm43455/` の 15 file（comment だけ）。file の先頭の出典の段落・文を 10 file（hw.h・chip.c・bcdc.c・event.c・sdpcm.c・nvram.c・bus.c・radio.c・station.c・bcm43455.c）から除いた。
- 引用の comment: `Value(s) referenced:` / `Procedure referenced:` の段落 111 個（hw.h 32、radio.c 24、chip.c 11、bus.c 7、radio.h 7、nvram.c 6、event.c 5、station.c 5、event.h 3、sdpcm.c 3、bcm43455.c 3、bcdc.c 2、bcdc.h・sdpcm.h・firmware.h 各 1）。どれも block の直前の複数行の comment の最後の段落。版と path は [values.md](../../ws206/phase004/values.md) と [ws206-p005](../../ws206/phase005/phase.md) の記録（OpenBSD src commit 28854fb0 の bwfm.c 1.113・bwfmreg.h 1.26・bwfmvar.h 1.31・if_bwfm_sdio.c 1.44・if_bwfm_sdio.h 1.3、Infineon wifi-host-driver bd99da7b の `WHD/COMPONENT_WIFI6/…`）。行の番号も同じ記録から写した。
- 本文の出典の名前: 引用の段落の外の OpenBSD・bwfm・WHD・Infineon・brcmfmac・Linux を 0 にした（「as other drivers of the chip do」「as bwfm(4) sets it」「WHD goes on too」「without WHD's offset」など）。残りは firmware の file の path（`/lib/firmware/cypress/…`・`brcm/brcmfmac43455-sdio.txt`、上流の file の名前）と `bcm43455.h` の「(Broadcom/Cypress)」（vendor の事実）。
- 開発の管理情報の削除: `plan/ws206/phase004/values.md`・`plan/ws206/phase005/phase.md` への参照（hw.h・chip.c・radio.c 2・bcm43455.c）、`ws206-p005`・`ws206-p008`（bcm43455.c）、`BUG-311`（chip.c・bcm43455.c）、`T1-632`（bcm43455.c）、設計の項目 `(R8)` 3 箇所（chip.c）・`(E6)` 3 箇所（bcm43455.c 2・station.c 1）、出典の版の代わりの日付「master of 2026-10-13」（hw.h、bwfmvar.h 1.31 に）。log の文字列は変えていない（log に管理情報は無かった）ので、直した試験・道具は無い。
- symbol の名前: 出典の接頭辞（bwfm_・whd_・brcmf_・BWFM_・WHD_・SBSDIO・SDIOD・WLC_・WL_ など）の identifier は code に無い（comment を除いた identifier の一覧で確かめた）。出典と同じ名前は ws206-p005 の測定（cid.py、p005 の file で 0、p005b で chip.c の引数を付け直し済み）から増えていない。新たな付け直しは無し。
- 出典の不明（推測で埋めていない物）:
  - Linux brcmfmac の `BRCMF_DCMD_SMLEN`（256）・`MAX_CAPS_BUFFER_SIZE`（768）（bcm43455.c の version・capability の読みの長さ）: file と版の記録が無かった。→ 解決（2026-10-15、ユーザーの回答（Q1 経由）「brcmfmacの定数はLinuxのドライバです。」）: 他の WS が取得した Linux 6.8.12 の tarball（`/home/awe/zedBSD-worktrees/p1/build/ws229-p001/linux/linux-6.8.12.tar.xz`）から brcm80211 だけを scratchpad に展開して確かめ、`brcmfmac/core.h:30` `BRCMF_DCMD_SMLEN 256`・`brcmfmac/feature.c:226` `MAX_CAPS_BUFFER_SIZE 768`（どちらも SPDX ISC）で値が一致した。citation を「Linux 6.8.12 drivers/net/wireless/broadcom/brcm80211/brcmfmac/core.h (…line 30…) and …/feature.c (…line 226…)」に直した（p002 の commit）。
  - WHD の `whd_clm.c`・`whd_bus_sdio_protocol.c` は tree の中の path の記録が無いので file の名前だけ（ユーザーの回答待ち、2026-10-15 時点。他の WHD の file は `WHD/COMPONENT_WIFI6/inc/`・`src/include/`・`src/`・`src/bus_protocols/` の path）。
  - SD Association の SDIO Simplified Specification の版が無い（CCCR の値、hw.h）。版は不明（ユーザー確認済み、2026-10-15「バージョンは分からないです」）。名前だけのまま。
  - `request_clock` の `bwfm_sdio_htclk()`、`work_scan` の `bwfm_scan_v0()`・`whd_wifi_scan()` は行の番号の記録が無く、関数の名前だけ。
  - station.c の接続の順の「the join, lines 2433-2490」は p005 の表の行の範囲だけで、関数の名前の記録が無い。
  - 出典の無い値（comment に明記）: `bus:rxglom`、country の revision −1。
- 確認:
  - `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk vmunix`: rc 0、warning 0、error 0。
  - `sh plan/ws206/tests/bcm43455-host-test.sh build/ws249/host`: PASS（firmware の cache が無いので FILES は無し）。
  - comment を除いた比較（`gcc -E -fpreprocessed -dD -P`、#define の行を含む、空白を正規化）: 21 file で差 0。
  - `python3 plan/tools/style-check.py src/drivers/wifi/bcm43455/*.[ch] --summary`: total 120（開始時と同じ、p002 で扱う）。
  - 未実施: 実機（Pi 4）の WiFi（WS の最後に T へ依頼の案を出す）、QEMU（対象外）。
