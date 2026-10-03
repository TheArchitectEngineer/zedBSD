# WS031 第51報 — DMC F1(parser):固定 blob を解析し version 2.20・MAIN+4 pipe payload を device 所有領域へ保存。GPU-free 173/0

ご指示の DMC 四単位のうち **F1(parser)を実装し、固定 blob の解析結果を確定**しました。version・選択 ID・offset・payload サイズは**正本 parser 本体に同じ blob を渡した結果**であり、推測で埋めていません。build 0 error/warning。GPU-free **ktest 173/0**。

---

## 1. F1 parser(dmc.{c,h} 新規、正本 intel_dmc.c 準拠)

- **packed on-disk 構造**を移植:css_header(128B)/fw_info(12B)/package_header(16B)/dmc_header_base(20B)/v1(128B)/v3(256B)。
- `parse_css → parse_package → dmc_set_fw_offset(stepping マッチ)→ 各 id parse_header`。
- **長さの単位をヘッダごとに保持**:CSS/package/v3 の header_len=DWORD、v1 header_len=byte、fw_size=DWORD、entry offset=DWORD。**entry offset は CSS+package を読み終えた位置からの加算**(ファイル先頭基準にしない)。加算・残サイズ検査の後にポインタを生成し、切詰め入力は拒否。
- **stepping/ID 選択順は正本のまま**:wildcard 比較、同一 ID は**最初に一致した entry を保持**(最後採用・最新 version 選択にしない)。rev 0c → STEP_D0 → step_name "D0" → {'D','0'} を device 情報から供給。
- **payload は device 所有 arena(blob と別)へコピー** → provider ハンドル解放後も有効。3 状態(entry 選択 / payload 保存 / program 書込)を別フィールドで扱い、単一 loaded フラグで兼用しません。

## 2. 実 blob の解析結果(fixture、正本 parser で取得)

| 項目 | 値 |
|---|---|
| **version** | **2.20**(CSS header から取得、固定代入せず) |
| package header ver | 2 / entries 6 / CSS len 128B |

| DMC ID | header_ver | entry off(dw) | start_mmioaddr | mmio | fw_size(dw) | payload(B) |
|---|---|---|---|---|---|---|
| MAIN | 3 | 6301 | 0x80000 | 7 | 6274 | **25096** |
| PIPEA | 3 | 12639 | 0x90000 | 9 | 2547 | 10188 |
| PIPEB | 3 | 15250 | 0x98000 | 9 | 3066 | 12264 |
| PIPEC | 3 | 18380 | 0x52000 | 5 | 566 | 2264 |
| PIPED | 3 | 19010 | 0x59000 | 5 | 566 | 2264 |

総 payload 52076B < arena 128KB。試験:実 provider の blob を**本番 parser** へ渡し、version==(2<<16|20)/entries6/pkg_ver2/**5 ID すべて present**/MAIN header_ver3/MAIN payload 25096B・payload≠0 を確認。**provider 解放後も payload 有効**(取得 blob と保存 payload の寿命分離を実証)。`parity_dmc_parse_reset` で arena を解放。

## 3. 実機(GPU 渡さず、image 8c10eb5e)

`CAS-SELFTEST PASS` / **ktest 173/0**(172→173)/ probe=NOT_RUN / selftest=PASS。firmware provider(第50報)・電源 HW 初期化(第49報 実機到達)は回帰保持。

## 4. 次(F2/F3/F4、同一単位で継続)

- **F2 program load**:前処理 → DMC イベント handler 処理 → preemption 禁止で各 id payload 書込(`DMC_PROGRAM(addr, i)=start_mmioaddr + i*4`、DWORD index)→ 復元 → **付随 MMIO は dmc_mmiodata() 経由で変換**(pipe DMC イベント既定無効化のため blob 値から変更、mmiodata[] 全件そのまま write しない)→ **ADL-P clock-gating WA(開始 A〜D / 末尾 C・D のみ、対称戻しに短縮しない)** → 保存状態。既存 MMIO/preempt 適合層を使用、新 timer・常時 forcewake は追加しない。診断は 取得/解析保存/書込開始/書込完了/worker 終了を分け、`has_payload=true` だけを完了証拠にしません。fake で各 id の payload をアドレス・DWORD・順序・件数で逐次照合(前後処理・付随 MMIO は別区分)。
- **F3 worker/電源参照/fini**:`intel_dmc_init` は DMC 参照取得 + state/work 準備し **queue して return**(worker 直呼びせず、別 CPU worker が init return 前に開始は許容)。**電源参照 3 者分離**(PCI probe / INIT / DMC ロード、DMC は成功時解放・失敗時保持、driver_remove の rpm-only 解放を流用せず、**DMC オブジェクト確保前に DMC 参照取得がありうる**ので参照所有を obj 内だけに置かない)。不在 fallback は 既定 -ENOENT かつ path 未指定で legacy `adlp_dmc_ver2_16`(解析失敗を fallback 扱いにしない、alias 偽装しない)。fini は **flush_work 相当(cancel_work_sync で代用しない、false=idle を失敗にしない)**→ DMC 参照処理 → payload/state 解放 → power-domain driver_remove → 下位資源。worker 使用中メモリを free しない。
- **F4**:`intel_dmc_init` の UNIMPL を実呼出へ置換(電源 HW 初期化の後、通常 probe に DMC 完了待ち障壁を足さず、後続未実装を次停止点、診断終了で DMC work 同期し採取)。GPU-free 4 試験(DMC-NORMAL/NO-FW/BAD-FW/FINI)後、実 GPU 一回。

**保持**:firmware provider、電源 HW 初期化(実機到達)、10ms tick/HAL 非変更、実 sleep/preemption/PCODE/CDCLK/D3/fuse/DC_off/VGA。GPU=vfio-pci、drm 非 blacklist、attach 先行。描画・EU 試験・baremetal・ハング比較は追加しません。

次報:F2 program load 本体(前後処理・dmc_mmiodata 変換・WA)、F3 worker/参照/fini、GPU-free 4 試験結果、可能なら実 GPU ロード結果。
