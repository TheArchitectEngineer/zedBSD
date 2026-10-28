<!-- awesome-plan project=zedbsd record=ws235-p003 -->

# ws235-p003 MAC・PHY の初期化と link（firmware 無しから）

- 親: [WS235](../ws.md)。
- 状態: in-progress（2026-10-29 06:4x P1、q1669-i01）。code と build まで。実機は未実施。
- 依存: [p002](../phase002/phase.md)。

## 範囲

open のたびに: OOB からの抜けと reset → PHY の電源 → MAC の設定（CLKREQ・UPS を切る、MAC の MCU を止めたまま、CSI 0x108）→ 割り込み・timer を閉じ reset →
EPHY の表（RTL8127）→ PHY の reset（広告を消す）→ PHY の microcode の版を読む → RTL8127 の PHY の表 → EEE・advanced EEE を切る → 全部の速さを広告して交渉。
link は kthread が 500 ms ごとに PHYSTAT を読み、carrier と `link_mbps` を出す。firmware（MAC・PHY の microcode の patch）は入れない（p001 の 3、判断待ち）。

## 記録

- 2026-10-29 06:4x P1: `src/drivers/ethernet/rtl8125.c` に足した（commit cad42f821）。RTL8127 の PHY の表は OpenBSD rge 1.48 の `rge_phy_config_mac_r27`
  （obsd-if_rge.c:2616-2899、`rge_phy_config_mcu` を除く）を scratchpad の変換の script で (write・modify・read, reg, mask, value) の 239 段の表にした。
  数の照合: 元の呼び出し 202（loop の本体 5 を引き、loop の展開 12・12・18 を足す）= 239 で一致。EPHY の表は `mac_r27_ephy` の 36 個。
- 疑い（未確認）: OpenBSD の EPHY の窓の address の mask は 0x7f（`RGE_EPHYAR_ADDR_MASK`）で、RTL8127 の表の offset（例 0x088・0x188）は 0x7f で切ると同じ 0x08 に
  なる。OpenBSD のまま写した。FreeBSD の hw の file（未取得）か実機の振る舞いで確かめる。
- build: `make ZEDBSD_CONFIG=plan/ws235/tests/config-amd64-rtl8127.mk BUILD=build/ws235/amd64 build/ws235/amd64/vmunix` → exit 0、warning 0（`build/tmp/ws235-build3.log`）。
- 実機の試験の script: `plan/ws235/tests/rtl-link.sh VMUNIX OUT`（NVMe の kernel を保存 → 試験の kernel → BootNext → ssh で `ifconfig rge0`・`ifconfig rge0 up`・
  5〜65 s の間の `ifconfig rge0`、centris の `ethtool enp94s0f1` → reboot で Debian → 保存した kernel を戻す → `OUT/summary.txt`）。未実行。

## 未実施

- 実機（RDNA 機の枠が要る）。OpenBSD と FreeBSD の差の表（p001 の残り、中 7）。
