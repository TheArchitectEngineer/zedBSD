# WS031 第E-87報：P3後段 完成 — 実機で `intel_display_driver_probe_noirq()` 完走。併せて PCI ID 読出しの不具合を発見（判断(A)の再確認をお願いします）

## 1. 結論

**P3後段は完成し、実機で `intel_display_driver_probe_noirq()` が最後まで通りました。**

```
i915: parity P3 intel_bw_init: obj=3 sagv_forced=1 qgv=0x4 psf=0x6 mask=0x10b pcode=0 sagv_status=1
i915: parity P3 intel_init_quirks: dev=0xffff subsys=ffff:ffff quirk_mask=0x0 hooks=0 dmi_available=0
i915: parity P3 intel_fbc_init: fbc_mask=0x1 enable_fbc=1 created=1 funcs=6
i915: parity P3 probe_noirq COMPLETE: global objs=4 (order: cdclk,dbuf,bw,pmdemand) mode_config max=16384x16384 cursor=256x256 async_flip=1
i915: parity attach end: reached=P3 outcome=BLOCKED where=intel_irq_install err=0
```

- **新frontier = `intel_irq_install`（P4）**。P3 は正本と同じ内容で終端しました。
- GPU-free 試験 **184 → 197 checks / 0 failures**。build 0 error/warning。
- teardown は display-state fini → DMC fini → power domains → … と逆順成立、二重解放なし。

## 2. 実装した内容（正本 `intel_display_driver_probe_noirq()` 末尾を全移植）

`intel_mode_config_init` → `intel_cdclk_init` → `intel_color_init` → `intel_dbuf_init` → `intel_bw_init` → `intel_pmdemand_init` → `intel_init_quirks` → `intel_fbc_init` → return 0。
正本の error label（`intel_dmc_fini` → `intel_power_domains_driver_remove`）は既存 teardown 順と一致していたので、失敗時はそこへ飛ぶだけで済んでいます。新規 `display_state.{c,h}`。

実装上、特に注意した点：

- **`intel_atomic_global_obj_init` を共通機構として1回だけ移植**。cdclk/dbuf/bw/pmdemand の4つが `display.global.obj_list` に **tail 追加**され、**挿入順が観測可能**（実機ログの `order: cdclk,dbuf,bw,pmdemand`）。
- **`intel_color_init`**：ADL-P(ver13) は `DISPLAY_VER != 10` で即 0。ver10 の linear degamma LUT は DRM property blob が無いため **`-ENOSYS`** を返します（未実装を静かな成功にしない、の原則どおり）。
- **`intel_bw_init` は ADL-P で no-op ではありません**。`intel_has_sagv` かつ ver 11..13 で **`icl_force_disable_sagv` が実際に走り、PCODE 取引（mbox 0xe）を発行**します。実機で `pcode=0`＝成功、`sagv_status` が DISABLED に更新されました。
- **`icl_max_bw_index` と `tgl_max_bw_index` は互いの変種ではありません**。走査方向（前進/後退）・比較（`num_planes >=` / `<=`）・既定値（`UINT_MAX` / `0`）が**すべて逆**です。両方を逐語移植し、**両者が別 group を選ぶ fixture** を作って差が出ることを試験で固定しました（推測で一方に寄せると静かに壊れる箇所です）。
- **2つの "bw 状態" を混同していません**：P2 で採取した HW 表（`display.bw.max[]` / `sagv.status`）と、ここで新規に作る atomic global state（`intel_bw_state`、`qgv_points_mask` 保持）は別物として保持。
- **`intel_fbc_init`**：ADL-P(xe_lpd) の `fbc_mask` は **`BIT(INTEL_FBC_A)` の1個だけ**です（MTL の XE_LPDP が A|B なので、取り違えると2個作ってしまう箇所。正本テーブルを確認済）。実機 `created=1 funcs=6(IVB)`。
- **`intel_init_quirks`**：`intel_quirks[]` 25件を逐語移植。DMI 側は `dmi_check_system` 相当が無いことを**記録**します（`dmi_available=0`。黙って省略しない）。
- **データ是正**：`parity_sagv_status` が正本と食い違っていました（**DISABLED が欠落**、NOT_CONTROLLED の値も相違）。正本値（UNKNOWN=0, DISABLED=1, ENABLED=2, NOT_CONTROLLED=3）へ是正しました。E-69 の DC 値是正と同種です。

### 追加した試験（+13、計 197/0）
DS-MODE（ADL-P値）／DS-MODE ladders（全arm）／**DS-INDEX（ver13=tgl→BIT1、ver11=icl→BIT0 で異なること＋psf同値はOR）**／DS-OBJ（順序・back-link・kref=1・funcs別・ver14 WA不発）／**DS-SAGV（PCODE 0xe / data 0x5 / status→DISABLED）**／DS-SAGV-SKIP（NOT_CONTROLLED で PCODE 0件）／DS-SAGV-FAIL（PCODE失敗でも `bw_init` は 0、`sagv.status` 不更新）／DS-ENOMEM（-ENOMEM 伝播、obj_list 不変）／DS-COLOR（ver10=-ENOSYS）／DS-QUIRKS／DS-FBC（vtd WA・tri-state・個数・funcsラダー）／DS-FINI。

## 3. ★発見：PCI ID の読出しが不正でした（判断(A)を覆します）

quirk 照合の入力が `dev=0xffff subsys=ffff:ffff` になっていたので、挙動を変えずに診断だけ入れて1回確認しました。同一実行内の比較です：

```
PCIID cached: ven=0xffff dev=0xffff rev=0x00 subsys=ffff:ffff
PCIID live  : ven=0x8086 dev=0x46a8 rev=0x0c subsys=1028:0b02
```

`drv_pci_device_vendor/product/revision/subvendor/subproduct`（キャッシュ済 PCI ヘッダ）が**すべて 0xffff / 0** を返します。live config read（`osdep_pci_read8/16`。同じ実行の P0 で `cfg_vendor=0x8086` と実証済の経路）は正しい実体を返します。

影響：

| 箇所 | 実際に起きていたこと | 結果 |
|---|---|---|
| **DMC**(E-86) | revid=0x0 → step **A0** を選択（正しくは rev 0x0c → **D0**） | MAIN payload **6237(A0)** vs **6274(D0)**、**差 37 = 12982 vs 13019 に完全一致** |
| **CDCLK**(E-75, 既存) | 同 accessor。revid=0x0 → STEP_A0 → Wa_22011320316 の **a-step table** を選択（rev 0x0c の正解は `adlp_cdclk_table`） | 実機で誤ったテーブルを使用中 |
| **quirks**(本増分) | 0xffff で照合 | `intel_quirks[]` に ADL-P 項目が無いため**結果は不変**（quirk_mask=0） |

**したがって、前回ご判断いただいた「12982 = 実機正として受理」は誤りでした。** 12982 は「誤った stepping(A0) の payload」で、rev 0x0c の正解は 13019 側です。私が前回提示した仮説（revision 由来の stepping 差）は方向としては当たっていましたが、「実機が正しい stepping を選んでいる」という前提が間違っていました。訂正しておわびします。

**本増分では診断のみで、挙動は変更していません。**

## 4. ご判断いただきたい点

修正は3箇所の `drv_pci_device_*` を live config read（P0 で実証済の経路）へ置換するだけで、HAL には触れません。ただし **CDCLK の table 選択が変わる**（a-step → adlp_cdclk_table）ため、既に受理済みの E-75 の実機挙動が変わります。

- **(A) 今すぐ修正を適用**し、CDCLK/DMC を正しい stepping(D0) で実機1回 再確認してから P4 へ【推奨】
- (B) 修正は当てるが、CDCLK は据え置き（DMC と quirks のみ live read へ）
- (C) 修正せず P4 へ進み、後でまとめて対処

私の推奨は **(A)** です。DMC payload が 13019 に戻り、CDCLK が正しいテーブルを引くことを1回で確認できます。

ご指示いただければ、そのまま P4（`intel_irq_install`）に着手します。P4 は工程表でお伝えしたとおり**唯一 HAL 圧が出うる箇所**なので、圧が出た時点で止めてご相談します。
