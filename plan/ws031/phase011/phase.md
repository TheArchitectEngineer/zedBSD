# WS031 p011 計画: 統合 — 増分A→B→C、実機 vkdemo 描画

全モジュールを結線し、増分A（三角形）→B（texture+depth）→C（vkdemo 実 shader）の順に実機ネイティブで描画・scanout する。おおまかな設計。

## Module と所有ファイル
- 結線・config: `platform/amd64/vmunix.mk`（vk source 群を `AMD64_I915_SOURCES` へ）、`Makefile`/`config/*`（vk ビルドスイッチ）、`plan/ws031/tests/config-vk-amd64.mk`
- harness: `plan/ws031/tests/run-vk-remote.py`、`vk-native-boot` 手順、独立 oracle（vkdemo の既存 ray-texture oracle を流用）
- 必要時のみ: `userland/base/libvulkan/`（capset 整合の最小差分。p002 の方針に従い、この Phase だけが触る）
- 触れない: 各モジュールの内部（完成済み `.c` は変更しない。不足は該当 Phase を uncleared で差し戻す）。

## 実装する公開インタフェース（規約・正本）
新規インタフェースは足さない。各モジュールの確定インタフェース（p002–p010 の doc/results）を使って結線する。増分ごとの到達点:
- 増分A: libvulkan（無改造）で単色三角形を submit → cmd→res→compile(定数FS)→pipe→cmdbuf→RCS0→wsi flip。実機で全画面に単色三角形。
- 増分B: texture＋depth を追加（res の image/sampler/descriptor、compile の sample、pipe の depth）。
- 増分C: `/bin/vkdemo` の実 vertex/fragment shader（回転 cuboid、checker）。実機で回転 cuboid 表示。

## 内部関数構成ガイド
なし（結線）。増分ごとに「必要な関数が前段 Phase で実装済みか」を確認し、欠けていれば該当 Phase を uncleared にして差し戻す（設計精度を上げて再委譲）。

## 依存
- 前段: p002–p010 の全モジュール（確定インタフェースと results）。
- WS029 core: 既存の attach/engine/request、f003。
- libvulkan（WS030）: 無改造が原則。

## 触れるファイル / 触れないファイル
- 触れる: build 配線、`plan/ws031/tests/**`、（capset 整合が要る場合のみ）libvulkan の該当箇所。
- 触れない: 各 vk モジュールの完成 `.c`（変更が要るなら差し戻し）、HAL、UAPI、WS029 core ロジック。

## 受け入れ条件と試験
- 実機ネイティブ（Latitude 5330）: 増分A/B/C の描画と scanout。独立 oracle が GPU 出力を使わずに画像照合（増分C は vkdemo oracle）。ホスト（QEMU/Venus/ANV）不使用。dmesg に compile/submit/flip ログ。
- build 3 構成、warning 0。
- ネイティブ起動での目視確認はユーザーが行う。

## 見積・制限
360 分。増分は段階受け入れ。あるモジュールの不足が判明したら該当 Phase を uncleared で差し戻し、設計 doc を更新してから再実装する（本 WS の運用方針）。

## 実機テスト手順の確立（2026-09-15）
ユーザー指示により、10.0.10.25 で i915/xe を起動時 blacklist し IGD を vfio-pci へ boot-time バインドする手順を確立（[vfio-passthrough-procedure.md](../tests/vfio-passthrough-procedure.md)）。再起動後 lsmod に i915/xe なし・IGD は vfio-pci・単独 IOMMU グループを確認。QEMU パススルー（rombar=0）が VFIO 初期化に成功。実行時 unbind の間欠性を排除。ビッグバンテストはこの状態の上で zedBSD i915+vk image を直接パススルー起動して行う。

## 設計修正（2026-09-15）: per-command decode と reply transport が p011 の実体

ビッグバン診断中に判明: p002–p010 で各モジュール関数は実装済みだが、
`res/pipe/cmdbuf/sync/wsi` の `_dispatch`（Venus wire → モジュール関数の
デコード）は EINVAL スタブのまま「integration で完成」と先送りされていた。
さらに i915.c の command 経路は `drv_i915_vk_command(vk, buf, bytes, NULL, NULL)`
で reply を捨てており、reply が libvulkan に返らない。よって p011 は「結線」
ではなく **decode + reply transport の実装**が実体。該当は p011 内で行う
（各モジュール .c に decode を足すのは p011 の結線作業と見なす。内部アルゴリズム
は変更しない）。

### libvulkan wire 形式（実測: userland/base/libvulkan、Venus wire-format 1）
- command: `command_begin` が u32 opcode, u32 flag(=1, reply要求) を書く。以後、各
  引数を LE で append。handle=u64 wire_id、pointer=u64 presence(0/1)、struct は
  sType(u32)+ext-present(u64)+[ext...]+fields の順。
- 例 vkAllocateMemory(21): [dev u64][pAllocateInfo present u64][sType=5 u32]
  [ext_present u64][(ext時: u32 type,u64 next,u32 val)][allocationSize u64]
  [memoryTypeIndex u32][pAllocator present u64=0][pMemory present u64=1]
  [memory wire_id u64]。reply(24B)= present u64 + identifier u64（＝新 wire_id）。
- reply レイアウト（command_execute が読む順）: [u32 opcode echo][u32 VkResult]
  [payload...]。reply-request flag=1 のコマンドのみ reply を書く。

### reply transport（MESA 拡張、実測 context.c/wire.c）
- libvulkan は各 transaction を vkSetReplyCommandStreamMESA(178) 等の reply-stream
  framing で包み、reply 用の別 resource（context->reply_resource, GPU_BLOB/RESOURCE_MAP）
  に executor が reply を書く。完了は reply trailer で検出。
- 実装方針: 178/180 を executor で解釈し、指定 reply resource + offset を session に
  記録。`drv_i915_vk_command` の reply 出力をその resource mapping へ書く（i915.c の
  NULL,NULL を実 reply バッファに差し替え）。framing [opcode echo][result] は
  cmd_dispatch が共通に付与（flag=1 時）、payload は各 _dispatch が append、result は
  モジュール errno→VkResult を backpatch。

### 実装増分（改訂）
- A0: reply framing 規約を cmd_dispatch に実装 + res_dispatch の memory/buffer/image。
  host fixture（wire encode→dispatch→object 生成 + reply 照合）で検証。
- A1: reply transport（178/180 + drv_gpu reply resource）を結線し、実 libvulkan の
  1コマンド往復を実機で確認。
- A→B→C: 既定どおり三角形→texture/depth→vkdemo。
