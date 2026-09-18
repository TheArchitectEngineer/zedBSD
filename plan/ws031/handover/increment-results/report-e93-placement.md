# WS031 E-93 報告: カーネル物理配置の可変化（ローダ＋HAL）

日付: 2026-09-18 / 対象: zedBSD amd64 UEFI ローダ・HAL / 検証: OVMF（chaos）、実機 ADL-P GPU 渡し（VFIO）、SeaBIOS（TCG）

## 結論

像の物理 8 MiB 上限（E-92 で縮小回避したもの）を恒久的に解消しました。ローダはリンク位置（2 MiB）が空いていればそこへ、塞がっていれば 1 GiB 未満の最下位の 2 MiB 整列空き領域へ像を置き、HAL は固定の仮想アドレスをその物理範囲へ写像します。

| 構成 | 結果 |
|---|---|
| 既定（リンク位置） GPU-free / GPU 実機 | ktest 338/0 ／ record_defaults rc=0・BLOCKED=verify_workarounds・ktest 338/0（**E-92 と同一**） |
| 再配置強制 `kernel_phys=0x2000000` GPU-free / GPU 実機 | 同上（載る位置が 32 MiB になっただけで結果は同一） |
| BIOS ローダ（SeaBIOS, TCG） | 従来どおり 2 MiB に載り HAL 起動（IRQ READY まで確認） |

実機でどちらの配置でも GPU 実行結果が変わらないことを確認済みなので、**既定運用は今までと同じ挙動**（像が 6 MiB を超えたときだけ自動で再配置が働く）です。

## レビュー後の変更内容

事前レビューで確定した方針（仮想固定・物理可変）から変えた点はありません。追加したのは「壊れたときの見え方」です。

- **契約を 1 か所に**: `bootloader/include/amd64-kernel-image.h`（リンク物理 2 MiB／整列 2 MiB／上限 1 GiB／最大像 16 MiB）。ローダ・HAL・handoff 検証・リンカ ASSERT が同じ値を参照します。handoff 形式は変えていません（`kernel_phys_start/end` が実配置を表すだけ）。旧ローダ（常に 2 MiB）とも互換です。
- **ローダ**: `A64 KERN LINK / SIZE / LOAD`（再配置時は `RELOCATED`）を必ず出力。配置に失敗したときは 1 GiB 未満の UEFI メモリ記述子を `A64 KERN MAP t=<type> <start> +<pages>` で列挙してから停止します。
- **切り分け用ノブ**: `zedbsd.cfg` に `kernel_phys=link`（従来どおりリンク位置のみ、塞がっていれば失敗）／`auto`（既定）／`0x…`（指定位置のみ）。実機で起動しなくなった場合は `link` で「再配置が原因か」を、`0x…` で「特定位置で再現するか」を、cfg の 1 行だけで確認できます。
- **HAL**: コンソール初期化直後に `A64 KERNEL link=… load=… (as linked|relocated by the loader)` を出力し、整列・上限・リンク像とのサイズ一致・報告 RAM（usable/boot-reclaim）内への完全包含を検証。不合格なら理由と 1 GiB 未満の範囲表を出して FATAL。像アドレス→物理の変換、W^X 窓、ダイレクトマップの text/rodata 境界、各アロケータの予約、handoff の固定値検査を、すべてこの「像の幾何」経由に統一しました。
- **shadow の予約**: 再配置すると、旧リンク物理範囲（2〜8 MiB）は起動窓から見えなくなります（その仮想アドレスは像自身を指すため）。早期ページ表ページはこの窓経由で書かれるので、この範囲をアロケータから恒久的に外し（最大 16 MiB、今は 6 MiB）、万一そこから早期表ページが取られたら FATAL する防護も入れました。

## 途中で見つけて直したもの

**shadow の幅**: 初版の再配置版は CR3 切替直後にトリプルフォルトしました（`-d int` で PF、CR2＝ダイレクトマップの 1 MiB、`amd64_ram_lookup` 内）。ローダは 2 MiB スロット単位で向け替えるため、像末尾からスロット末尾まで（0x7d0000〜0x800000）も再配置先の直後を指しますが、予約は像のサイズ分しかなく、その余りから取られた早期表ページが窓越しに別の物理へ書かれていました。shadow を 2 MiB 境界に丸めて解決し、上表の結果はすべて修正後のものです。

## テスト

- 新規ホストテスト `plan/ws031/tests/kernel-placement-host.c`（ELF 計画・再基底化・HAL 規則、ASan/UBSan 込み）PASS。既存 ws025 `memory-handoff-host` に再配置の受理／不整列・超過・窓外の拒否を追加、PASS。
- 補足: ws003 `x86-parameter-handoff-test` は変更前から旧名（`ZEDBSD_*`）のままでビルドできません。本件とは無関係なので触っていません。
- 補足: SeaBIOS 経路は sudo が使えず TCG で確認したため、ktest は実時間タイムアウト系の FAIL が出ます（配置とは無関係。KVM 実行では未計測）。

## 実機で起動しなくなった場合の手順（想定）

1. ローダ画面の `A64 KERN LOAD` を見る。`0x200000` なら再配置は起きていない（原因は別）。
2. 再配置していたら `kernel_phys=link` で従来動作に戻し、`A64 KERN MAP` の列挙と `A64 KERNEL` 行（HAL の理由）を送ってください。
3. 再配置が原因なら `kernel_phys=0x…` で位置を変えて再現条件を絞れます。

## 次

E-92 の残と同じ `__engines_verify_workarounds`（SRM request の投入。着手前に一言ください）。像上限が外れたので `OSDEP_DMA_MAX_MAPPINGS` は次に像が育ったときに 256 へ戻します。

git commit / push はしていません。HAL の外部インタフェース（tick・timer・waitq）は不変で、変更は HAL 内部の配置機構のみです。
