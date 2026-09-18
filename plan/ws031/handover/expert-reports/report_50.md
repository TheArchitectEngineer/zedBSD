# WS031 第50報 — DMC 第一段:参照 firmware(i915/adlp_dmc.bin)を固定同梱し read-only provider を接続。GPU-free 172/0

電源 HW 初期化(第49報、実機到達)を受入完了として閉じ、次単位 **intel_dmc_init** に着手しました。ご指示の第一着手=**参照環境の i915/adlp_dmc.bin を確保しサイズ・SHA-256 を固定、その bytes を返す firmware provider を接続**を完了しました。build 0 error/warning。GPU-free **ktest 172/0**、probe=NOT_RUN。

---

## 1. firmware 識別情報(manifest 追加)

| 項目 | 値 |
|---|---|
| 要求名 | `i915/adlp_dmc.bin` |
| 実際の取得元 | 参照環境 chaos:`/lib/firmware/i915/adlp_dmc.bin`(最新版を DL せず固定ファイル使用) |
| 非圧縮 bytes サイズ | **79088** |
| SHA-256 | `3516de2e134ddcf3b319c75d2e437779fecbd58cbb77234bd6f297c544e92ccb` |
| byte-sum checksum | `0x002b075a`(GPU-free 完全性照合用) |

version(2.20)は**ファイル名から決めず**、後段の CSS header 解析結果で確定します。GuC/HuC とは別の firmware です。

## 2. read-only firmware provider(HAL 非追加、VFS 非使用)

- 同じ blob を**起動 image へ同梱**:`firmware_adlp_dmc.c`(79088 byte の C 配列、参照 blob の byte-for-byte 複製)。
- `osdep/firmware.{c,h}`(request_firmware()/release_firmware() 契約対応):
  - `osdep_request_firmware(fw, name)` — 名前一致で `data`/`size` を返し 0、**不在は -ENOENT かつ data=NULL**。
  - `osdep_release_firmware(fw)` — ハンドル破棄のみ。**静的 blob を free しません**(所有権は provider 側の静的 read-only データ)。
- これは適合層の設計で、VFS 全体やユーザ空間 firmware サービスへは広げていません。HAL API も追加していません。
- 試験(GPU-free、+3):名前一致で 79088 byte + checksum 一致 / release 後ハンドル 0(blob 非 free)/ 不在名で -errno・bytes 無し。

## 3. 実機(GPU 渡さず、image f96cd9eb)

`CAS-SELFTEST PASS` / **ktest 172/0**(169→172)/ probe=NOT_RUN / selftest=PASS。第49報の電源 HW 初期化(実機 intel_dmc_init 入口到達)は回帰として保持。

## 4. 次(DMC 本体、一単位として継続)

ご指示の順で継続します(parser だけで区切らず、worker・program load・DMC 固有電源参照・fini まで):

- **parser**(正本 intel_dmc.c/.h/intel_dmc_regs.h):CSS/package/DMC header(v1/v3)解析、stepping 選択(rev 0c→STEP_D0)、size/MMIO 範囲検査、payload 保存。main 決め打ちせず**選択された各 DMC ID** を扱い、ヘッダを飛ばさない。bytes と DWORD の単位、offset 基準、**取得 blob と保存 payload の寿命分離**を確認。解析結果=要求名/stepping/選択 ID/payload サイズ/付随 MMIO 件数(期待値は実 blob を正本解析した結果から)。
- **非同期 worker**:`intel_dmc_init()` は DMC 状態・work・参照を準備し **queue して return**(同期版に短縮せず)。worker が 取得→解析→program load→参照・電源処理→firmware 解放。DMC 状態・worker 引数・参照 display 状態は **device 所有領域**(一時 local でない)に置き、既存の共有 workqueue/completion を再利用。
- **program load**:既存 MMIO 適合層。前処理→DMC イベント無効化→**preemption 禁止 payload 書込**→firmware 由来の付随 MMIO→末尾処理。`DMC_PROGRAM(addr, i)` は **4 byte 刻み(DWORD index)**(file offset/CPU/GPU VA と混同しない)。IRQ 長時間禁止・巨大 spinlock はしない。診断:取得済/解析保存済/書込開始/書込完了/worker 終了(`has_payload=true` だけを完了証拠にしない)。
- **電源参照 3 者分離**:PCI probe / 電源 HW 初期化 INIT 参照 / DMC ロード参照 を別管理。DMC は自参照を取得→**成功時解放/失敗時保持**。`driver_remove` の「rpm-only だけ解放」を DMC の通常 domain 参照解放へ流用しません。
- **fini**:work 完了同期(**flush_work 相当、cancel_work_sync で代用しない**)→ DMC 参照処理 → payload/DMC 状態解放 → 既存 power-domain driver_remove → 下位資源。worker 使用中メモリを free して cleanup=1 にしない。
- **GPU-free 4 試験**:DMC-NORMAL(実 blob 取得解析ロード完了、DMC 参照のみ解放・親 INIT 残)/ DMC-NO-FW(実不在→fallback+参照保持+fini)/ DMC-BAD-FW(header 破損→不正書込へ進まず途中確保回収)/ DMC-FINI(queue 済/実行中→完了同期前に MMIO/device/payload 破棄しない)。既存 172 回帰保持。
- 揃ったら **実 GPU 一回**(通常位置ロード、独自同期障壁を足さず、後続未実装なら次停止点、診断終了で DMC work 完了待ち→結果採取→cleanup)。

**保持**:電源 HW 初期化(実機到達)、10ms tick / HAL 非変更(確定)、実 sleep(periodic)/preemption/PCODE/CDCLK/D3/fuse/DC_off/VGA。GPU=vfio-pci、drm 非 blacklist、attach 先行。描画・EU 試験・baremetal・DMC 有無のハング比較は追加しません。

次報:DMC 本体(parser/worker/program load)と provider・work 完了同期・cleanup の差分、4 組の試験結果、firmware 識別(parsed_version/選択 ID/payload サイズ)。実 GPU へ接続できた場合はそのロード結果も。今回の受入=「参照と同じ DMC firmware を実 GPU へロードし、非同期処理と参照を安全に終了」までを目標に継続します。
