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
