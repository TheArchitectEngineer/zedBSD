<!-- awesome-plan project=zedbsd record=ws235-p007 -->

# ws235-p007 他の chip の版の表と差の hook（8125・8125B・8125D・8126、8122・8121・8120）

- 親: [WS235](../ws.md)。
- 状態: cleared（2026-10-29 11:0x P1、q1669-i04。実機は RTL8127 だけ、他の chip は未確かめ）。
- 依存: [p003](../phase003/phase.md)。

## 範囲

版の表（TXCFG の値 → family・revision）と、chip ごとに違う手順（MAC・EPHY・PHY）の hook。実機が無い chip は build と RTL8127 の非退行まで。

## 実装（ae8cb27df）

- 版の表（p002 から）: RTL8125 0x609・8125B 0x641・8126 0x649/0x64a・8125D 0x688/0x689・8127 0x6c9（TXCFG の 0x7cf00000 の mask）。MAC の差は family の switch（p002〜p004）。
- EPHY: RTL8125 は表 12 個＋両 lane の read-modify-write（`configure_ephy_8125`、`ephy_read`・`ephy_modify` を足した）、RTL8125B は表 10 個、RTL8127 は p003 の表、
  RTL8125D・8126 は reset の値のまま（FreeBSD rge と同じ）。
- PHY: `phy_steps_of` が family と revision で表を選ぶ: 8125（129＋patch 2）・8125B（36）・8125D 1（141＋2）・8125D 2（34＋2）・8126 1（242＋2）・8126 2（130）・8127（p003）。
  `RTL_PHY_STEP_PATCH` の step（PHY の patch mode に入る・出る、`set_phy_patch_mode`、0xb820 bit 0x0010 → 0xb800 bit 0x0040 を最大 1000 × 100 us、状態が追わなければ log して続ける）。
  PHY の microcode の load（FreeBSD の `rge_phy_config_mcu`）は RTL8127 と同じく入れない（firmware の扱いは p003 の判断のとおり）。
- 値と手順の出典は FreeBSD sys/dev/rge（main 8fa55fdcae4ecbd2ec13ef91cc9d172095e21189、BSD）の if_rge_hw.c（`rge_phy_config_mac_r25`〜`r26_2`・`rge_ephy_config_mac_r25`・
  `rge_patch_phy_mcu`・`rge_read_ephy`）と if_rge_microcode.h（`mac_r25_ephy`・`mac_r25b_ephy`）。block ごとに書いた。表の書き起こしは helper の agent が
  script で展開し、件数を source と突き合わせた（P1 は 8125B を目で照合）。
- RTL8122・8121・8120: 版の値が分からず表に入れない。Linux の r8169（2026-10-29 に git.kernel.org から GET、`rtl_chip_infos`）も FreeBSD rge も持たない（8125BP 0x681・8125CP 0x708・
  RTL8125K 0x68a・RTL9151A 0x68b は r8169 にあるが、この WS の名前に無く、手順も無いので入れない）。

## 確かめ

- build: `make ZEDBSD_CONFIG=plan/ws235/tests/config-amd64-rtl8127-perf.mk BUILD=build/ws235/amd64-n build/ws235/amd64-n/vmunix` exit 0、warning 0。
- RTL8127 の非退行（RDNA 機、[p006](../phase006/phase.md) の final-1）: link 10000Mbps、ping・ssh・SSH 100 MB 両向き・tcp-perf 30 s 両向き（4935・3360）。
- 他の chip は実機が無く未確かめ。
- 見直しの点: FreeBSD の `rge_phy_config_mac_r26_2` の 41 個の表の 20 番目の 0x82a0 が 0xb87e（data）に書かれる（r26_1 では同じ所が 0xb87c（index）への書き込み）。
  source のとおりに写した（reference の誤りか意図かは未確認）。
