# WS031 第E-87報 追補：PCI ID 修正を適用、実機で stepping 是正を確認（payload 12982 → 13019）

判断(A)を承認いただき、即時に修正を適用して実機1回で確認しました。

## 1. 修正内容

`drv_pci_device_*`（キャッシュ済 PCI ヘッダ。実機で全 0xffff/0）を使っていた3箇所を、**live config read**（`osdep_pci_read8/16`。同一実行の P0 で `cfg_vendor=0x8086` と実証済の経路）へ置換しました。HAL 非変更です。

- CDCLK stepping
- DMC stepping
- quirk の PCI ID（device / subsystem vendor / subsystem device）

cached-vs-live の診断ログは記録として残しています。build 0 error/warning。

## 2. 実機での確認結果

```
intel_dmc_init: revid=0xc step=D0                            (修正前: revid=0x0 step=A0)
PCIID cached: ven=0xffff dev=0xffff rev=0x00 subsys=ffff:ffff
     | live: ven=0x8086 dev=0x46a8 rev=0x0c subsys=1028:0b02
intel_init_quirks: dev=0x46a8 subsys=1028:0b02 quirk_mask=0x0 (修正前: dev=0xffff subsys=ffff:ffff)
intel_dmc_fini (load_seq_completed=1, payload_writes=13019, dmc_ref_held=0)   (修正前: 12982)
DMC ver=2.20 payload=6274/2547/3066/566/566                                    (修正前: MAIN=6237)
probe_noirq COMPLETE / reached=P3 BLOCKED where=intel_irq_install err=0
```

- **`payload_writes` が 12982 → 13019 となり、GPU-free フィクスチャ（stepping `'D','0'` ハードコード）と完全一致**しました。MAIN payload 6237(A0) → 6274(D0)、差 37 の説明が閉じました。
- quirk は live ID でも `intel_quirks[]` に ADL-P 項目が無いため `quirk_mask=0` のまま（**結果は不変、入力だけが是正**）。
- GPU-free 回帰：`CAS-SELFTEST PASS` / **ktest 197 checks, 0 failures** / selftest=PASS。

（注：実機の ref 構成では attach 完走後 ktest 実行中に 120s タイムアウトで打ち切られます。従来どおり ktest の数値は GPU-free 側で採取しています。）

## 3. 副産物の発見：CDCLK の忠実性ギャップ（本デバイスには無影響）

CDCLK の table 選択が変わるはずと申し上げましたが、**実際には変わりませんでした**。理由を確認したところ、`parity_intel_init_cdclk_hooks` の3分岐が**すべて同じ `adlp_cdclk_table` + TGL funcs を代入**しており、正本の **`adlp_a_step_cdclk_table` が未移植**でした（コメントだけ "not this device"）。正本の a-step 表は内容が別物で、**179200 のエントリを持ちません**。

- 本デバイス（rev 0x0c = D0）の正解は `adlp_cdclk_table` なので、**実機の挙動は正しい**です（cdclk=179200 vco=537600 は修正前後で不変）。
- 修正前は「A0 と誤判定したが、分岐が同一実装だったため偶然正しい表を引いていた」状態で、修正後は**正しい理由で正しい表**を引いています。
- 残ギャップは A0/A1 シリコンでのみ顕在化します（Wa_22011320316 未実装）。本機では再現不能なため、**台帳に分離記録**し、深追いはしていません。次増分以降のご判断事項として残します。

## 4. 現在地と次

- **P3 は完成**：`intel_display_driver_probe_noirq()` が実機で完走、frontier は **`intel_irq_install`（P4）**。
- 台帳 E-87 / E-87b 追記済み。

ご指示がなければ、工程表どおり **P4（`intel_irq_install`）** に着手します。P4 は**唯一 HAL 圧が出うる箇所**なので、既存の MSI-split / runner を維持したうえで、圧が出た時点で止めてご相談します。
