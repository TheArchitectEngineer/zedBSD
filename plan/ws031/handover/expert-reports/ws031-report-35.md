# WS031 レポート 35 — 実装の現状（E-130 時点）: リファクタリング方針決定のためのソースツリー・レビュー依頼

Date: 2026-09-21
対象ツリー: `~/zedBSD-gpu`（centris、未コミット差分あり。ユーザーが "WIP" でコミットする予定）
台帳: `plan/ws031/results-ws031.md` E-127〜E-130

このレポートの目的: 専門家にソースツリーを読んでもらうための地図。**リファクタリング方針はユーザーが決める**。本書は事実と論点だけを示し、方針は提案しない（§8 の論点は「決めること」の一覧）。

---

## 1. いま動いているもの（すべて Latitude 5330 実機、QEMU passthrough）

| 到達点 | 内容 | 証跡 |
|---|---|---|
| E-127 | `/bin/vkdemo`（無改造）→ `libvulkan`（無改造）→ `/dev/gpu0` → カーネル内 Vulkan executor → i915 → GPU。offscreen の 1 frame が独立オラクルで mismatch 0 | `handover/vk-e127/vkdemo-5330-frame1.png` |
| E-128 | shader を**カーネル内の自前 compiler**（SPIR-V → scalar IR → Gen12 EU）で生成。Mesa brw の kernel と同一 hash | `vkdemo-5330-own-compiler-2500ms.png` |
| E-129 | VK_KHR_display / swapchain で内蔵 eDP panel に表示（点灯・flip・停止は Linux 移植の LCD 経路） | `vkdemo-5330-lcd-e129.jpg` |
| E-130 | **画素の CPU コピーを撤去**。clear / copy / blit を GPU で実行し、表示は共有 blob を GPU で panel buffer へ拡大コピー（40 s で 726 frame、約 18 fps） | `vkdemo-5330-lcd-e130-shared.jpg` |

いまの到達範囲は「正常系 1 本の疎通」。未実装の経路は `XXX:` コメントと kernel message で名乗る方針で、空成功は置いていない（ユーザー方針）。

---

## 2. ソースツリーの地図

```
userland/base/libvulkan/          27.3k 行  Vulkan ICD 相当（標準 Vulkan API → ioctl のワイヤ形式）
src/drivers/gpu/gpu.c, gpu-fence.c 7.3k 行  GPU core: /dev/gpuN、session、blob、fence、display/scanout/share の
                                            ops 表、recovery（stop/isolate/fault）契約
src/drivers/gpu/venus/             7.3k 行  virtio-gpu (Venus) backend（i915 とは別の backend。WS031 は触っていない）
src/drivers/gpu/i915/
  *.c（legacy 層）                 8.9k 行  i915.c（ops 実装 2.2k）、gem / ggtt / ppgtt / request / lrc / engine /
                                            irq / uncore、selftest.c（2.6k）
  vk/                             12.0k 行  カーネル内 Vulkan executor + shader compiler
  parity/                         36.0k 行  Linux i915 の「正本」移植層（GT 初期化、電源、DMC、IRQ、reset …）
  parity/lcd/                     26.0k 行  Linux display の移植（modeset、DDI、DPLL、WM、backlight …）
  parity/dp/                       4.7k 行  eDP の AUX / PPS / EDID
```

### 2.1 層と責務

1. **GPU core**（`gpu.c`）: デバイス非依存。open ごとに session を作り、ioctl（`G*`）を backend の ops 表へ流す。blob、fence、display lease、scanout constraints、share（export/import）、recovery（stop_begin / stop_poll / isolate / fault）の契約はここに定義されている。
2. **i915 ops 層**（`i915.c`）: GPU core の ops 実装。session ごとに PPGTT、context、object 表を持つ。Vulkan コマンド列は `vk/` へ渡す。
3. **legacy 層**（`gem.c ppgtt.c request.c lrc.c engine.c ...`）: WS029 期に書いた独自 i915。メモリ・アドレス空間・request 待ち行列は現役。ハードウェアに触れる 6 関数（LRC 生成・破棄、ELSP kick、engine/GT reset、recover）は `parity/legacy_shim.h` の `#define` で parity 側に**名前ごと差し替え**ている（`PARITY_SHIM_REDIRECT`、legacy ファイル自体は無改造）。
4. **parity 層**（`parity/`）: Linux i915 を関数単位で移植した「正本」。GT の初期化・workaround・submit、display の電源・modeset・flip、eDP を担う。`probe.c`（2.7k 行）が起動列で、試験 build flag によって分岐する。
5. **resident mode**（`-DPARITY_RESIDENT=1`）: parity で初期化した GPU を `/dev/gpu0` として公開し、`legacy_shim.c` の**単一の serving thread** が全 GPU 仕事を同期実行する。`-DPARITY_RESIDENT_DISPLAY=1` で display / scanout / share ops も登録。
6. **vk executor**（`vk/`）: libvulkan のワイヤ形式（opcode + payload。`vkc.[ch]` + 生成 codec）を decode し、object を作り、command buffer を記録し、submit 時に Gen12 の 3D batch を組んで `parity_shim_run_sync()` で実行する。

### 2.2 vk/ の内訳（二系統が同居している点に注意）

| file | 行 | 役割 | 経路上か |
|---|---|---|---|
| `cmd.c` | 501 | decoder と opcode 振り分け。**まず gfx-obj → gfx-rec → 旧 module の順に問い合わせる** | ○ |
| `inst.c` | 632 | instance / device / queue、format 表、limits、submit の外側 | ○ (E-127) |
| `gfx-obj.c` | 1085 | memory / buffer / image / view / sampler / descriptor / render pass / pipeline | ○ (E-127) |
| `gfx-rec.c` | 943 | command pool / buffer、記録、vkQueueSubmit（操作列を順に実行） | ○ (E-127) |
| `gfx-draw.c` | 1515 | draw → Gen12 state + batch。E-130 の rect primitive（fill/copy）もここ | ○ |
| `spirv.c` / `compile.c` / `eu.c` | 1089 / 569 / 550 | SPIR-V → scalar IR → Gen12 EU 命令 | ○ (E-128) |
| `vkc.c` + `codec-generated.inc` | — | ワイヤ codec（libvulkan の `codec.c` から生成） | ○ |
| `res.c` `pipe.c` `cmdbuf.c` `sync.c` `wsi.c` `display.c` | 900 / 698 / 862 / 423 / 174 / 68 | **E-127 以前の設計**。gfx-* が処理しない opcode だけがここに落ち、ほとんどは「XXX unimplemented opcode」 | ×（host fixture は維持） |

---

## 3. 1 frame の流れ（E-130 の表示経路）

```
vkdemo ── vkCmdDraw ... vkCmdBlitImage(swapchain image → 共有 linear image) ── vkQueueSubmit
  │ libvulkan: 描画用の open と表示用の open（別 session）
  ▼
[描画 session]  gfx-rec: 操作列 → gfx-draw: draw batch / rect batch → parity_shim_run_sync()
  │ 共有 image は GPU_RESOURCE_EXPORT → i915_share_export（GEM object の参照数 +1）
  ▼
[表示 session]  GPU_RESOURCE_IMPORT(SCANOUT) → i915_share_import（同じ backing の alias を表示 session の PPGTT に bind）
  │ GPU_DISPLAY_PRESENT(FIFO|BLOB)
  ▼
resident_display.c rd_present ── ioctl thread で rect の kernel を準備 ──► serving thread へ PRESENT_BLOB
  ▼
legacy_shim.c shim_present_blob:
   panel の 2 buffer を表示 session の PPGTT へ uncached(PAT 3) で map（初回のみ）
   → rect batch（alias を sample、4 倍整数拡大・中央寄せ、裏 buffer へ RT write）→ 同期実行
   → parity_lcd_resident_flip()（Linux 移植の同期 flip）
```

- 表示の点灯・停止は `parity_lcd_kernel_resident_run()` が担う（LCD-C、E-119 で検証済みの本体）。serving thread は最初の present でこの関数に入り、`in_window` callback の中で GPU 仕事と present を処理し続ける。release で抜けて Linux の停止経路へ戻る（**制御の反転**。§8 論点 4）。
- COPY route（CPU で拡大コピー）は fallback として残っている。libvulkan は constraints に SHARED があれば SHARED を選ぶ。

---

## 4. shader compiler（E-128）

- `spirv.c`: SPIR-V → 直線の scalar IR（分岐なし、float のみ）。
- `compile.c`: IR → EU。冒頭の "Register conventions" に VS/FS payload、補間（原点 + d1·b1 + d2·b2）、URB write / sampler / RT write の SEND が書いてある。
- `eu.c` + `linux/eu-encoding-gen12.inc`: Gen12 命令の encoder。SWSB は**全直列**（正しいが遅い）。
- 検証方法: Mesa の `gentool`（asm/disasm + validator）を判定者にした host 試験（`plan/ws031/tests/run-vk-gentool-test.sh`）。以前は host model が encoder と同じ表を共有していたため、誤りに気付けなかった。
- 受理範囲: 入力 3、varying 3、sampled image 1、push constant は VS のみ、SIMD8 のみ。
- E-130 の rect 用 FS 2 本（fill / copy）も、この compiler で IR から作っている。

---

## 5. build 構成

| flag | 意味 |
|---|---|
| （なし） | 通常の i915（legacy 層のみ）。build OK、実機 sweep は WS029 以降未再実行 |
| `CONFIG_DRIVER_PCI_I915_PARITY=y` | parity 層を link |
| `-DPARITY_RESIDENT=1` | parity で初期化した GPU を /dev/gpu0 として公開（serving thread） |
| `-DPARITY_RESIDENT_DISPLAY=1` | + display / scanout / share ops（explicit VBT を伴う） |
| `-DPARITY_RESIDENT_SERVE_S` / `_STOP_ON_CLOSE` | 試験 run を有限時間で終わらせる |
| `-DPARITY_*_TEST=1`（LCDB/LCDC/LCDD/EU/TEX/DRAW/AUX/HDMI_*/DUAL/N1/T3/…、約 20 種） | 起動時に 1 つの実機試験を走らせる build |
| `-DI915_VK_REFERENCE_KERNELS=1` | 比較用: Mesa が生成した kernel を使う |
| `-DI915_VK_GFX_DUMP=1` | 1 frame 目を serial へ dump（オラクル用） |
| `-DGPU_IOCTL_TRACE=1` | ioctl の失敗を log |

試験用コードの多くはカーネル本体に同居している: `parity/ktest.c` 5.7k 行、`parity/eu_test.c` 2.2k 行、`parity/lcd/*ktest*`、`lcd_fake_hw.c`、`dp_fake_hw.c`、ファームウェア blob（DMC、VBT）の C 配列 7.5k 行。

---

## 6. 試験基盤

| 種類 | 入口 | 内容 |
|---|---|---|
| host fixture | `plan/ws031/tests/run-vk-host-tests.sh` | cmd / spirv / lower / res / resdispatch / sync / eu / compile / pipe / cmdbuf（10/10 PASS） |
| gentool | `run-vk-gentool-test.sh` | EU encoder と compiler の出力を Mesa の assembler/validator で確認 |
| 実機 | `vkloop-hw.sh` (`oracle` / `display [ms]` / `display live`) | build → 5330 へ転送 → QEMU passthrough → serial log。oracle は Python の独立実装と画素比較 |
| ktest（カーネル内） | `parity/ktest.c` ほか | 移植層の単体試験。model backend（fake HW）と実 HW の両方 |
| 撮影 | Windows 側カメラ | LCD の目視確認の補助（判定の一次根拠は log と読み戻し） |

---

## 7. 名乗っている未実装（XXX）— 分類済み

grep 件数（E-130 時点）: `legacy_shim.c` 21、`gfx-obj.c` 19、`gfx-rec.c` 14、`gfx-draw.c` 11、`inst.c` 8、`resident_display.c` 6、`i915.c` 4、その他 vk/ parity/ で 1〜2 ずつ。

**A. 実行モデル（最も構造的）**
- serving thread 1 本が全 GPU 仕事を**同期**実行（submit は完了まで戻らない）。RCS0 のみ、BCS0 は record だけ。
- hang は timeout で失敗するだけ。engine reset / GT reset / recover は入口で log して失敗する（parity に `__intel_gt_reset()` はあるが未接続）。
- ring は wrap しない前提。context record は固定数。
- fence / semaphore は「前の submit はすべて完了している」ことに依存している。

**B. Vulkan 機能範囲**
- image: 2D / 1 level / 1 layer / 1 sample / linear のみ。depth image の copy・sample は不可、stencil は書かない。
- clear は render area を無視して全面。mirrored blit は拒否。
- blend、dynamic state、descriptor array・buffer descriptor・descriptor copy、secondary command buffer、複数 subpass は、作成時に拒否するか名乗る。
- 1 op = 1 batch（rect も draw も個別に同期実行）。
- limits / format 表は vkdemo の疎通に必要な分だけ（`inst.c` 冒頭）。
- external memory の宣言は読むだけ（共有は blob 層で成立している）。

**C. compiler**
- 分岐なし、float のみ、SIMD8 のみ、SWSB 全直列、入出力数の上限が小さい。

**D. 表示**
- output / plane / lease 各 1、panel の native mode のみ（小さい mode は整数倍拡大で受理）、hot-plug event なし。
- present は同期（flip 完了で戻る）。表示用 address space は同時に 1 つ、map した VA は再利用しない。
- panel 点灯に失敗した後の再試行なし。

**E. 共有・object 管理**
- 共有は同一 device 内のみ。
- object 表は device 単位（session 単位ではない）。失敗時の途中解放なし（happy path only）が gfx-obj / gfx-rec に数か所。

---

## 8. リファクタリングで決めること（論点。推奨はしない）

1. **i915 の二重構造**: legacy 層（WS029）と parity 層（Linux 移植）が並存し、`#define` による名前差し替えでつないでいる。gem / ppgtt / request は legacy、context / submit / 初期化 / 表示は parity。どちらに寄せるか、境界をどこに引くか。
2. **vk/ の二系統**: E-127 以前の `res/pipe/cmdbuf/sync/wsi/display.c` は経路からほぼ外れているが、host fixture が依存している。gfx-* への統合と fixture の扱い。
3. **executor の置き場所**: Vulkan executor と shader compiler がカーネル内にある。この構成を維持するか、どこまでをカーネルに残すか（E-106 の決定の再確認）。
4. **serving thread と表示の制御反転**: LCD の実行本体が serving loop を callback として抱える形。非同期 submit、複数 engine、非同期 flip へ進むときに、この構造を保つか。
5. **試験コードと本体の分離**: 約 20 種の `PARITY_*_TEST` build、カーネル内 ktest（7k 行超）、fake HW、firmware の C 配列。build flag の組合せ爆発の整理。
6. **「parity」の粒度**: 関数単位の Linux 移植（`*_port.c`、正本との対応を保つ設計）を今後も続けるか、zedBSD 側の抽象に畳むか。
7. **エラー経路の方針**: 現在は happy path + 名乗る。recovery 契約（GPU core の stop/isolate/fault）を実装へ落とす順序。

---

## 9. 読み順の提案（レビュー用）

1. `src/drivers/gpu/i915/parity/legacy_shim.h` → `legacy_shim.c`（resident mode の心臓部、全体の境界がここに集まっている）
2. `src/drivers/gpu/i915/i915.c`（ops 表、session、share）と `gpu.c` の ops 契約
3. `vk/cmd.c` → `vk/gfx.h` → `gfx-rec.c` → `gfx-draw.c`（1 frame の実行）
4. `vk/compile.c` 冒頭の規約 → `eu.c`
5. `parity/resident_display.c` → `parity/lcd/parity_lcd_kernel.c`（`resident_*`）
6. `parity/probe.c`（起動列と試験 flag の分岐）

付録: 変更の多い未コミット差分（E-128〜E-130）は `git status src/drivers/gpu/i915` を参照。
