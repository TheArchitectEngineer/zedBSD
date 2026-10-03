# WS029 f003: ネイティブ起動時のスキャンアウト保持（GGTT framebuffer 保存）

## 症状
実機ネイティブ起動は成功するが、i915 driver 読み込み時点で LCD の最下位数行だけ正しく、その上が乱れる。

## 原因（scanout/GGTT、LCD クロックではない）
Gen12（ADL）ではディスプレイの primary plane が framebuffer を GGTT 経由で読む。firmware/ZBL6 が用意した
表示 framebuffer は GMADR アパーチャ（BAR2）経由で GGTT にマップされ、LCD はその GGTT アドレスを scanout する。
`drv_i915_ggtt_start` が 100万エントリの GGTT を全部 scratch で上書きしていたため、この表示マッピングも破壊され、
LCD が scratch ページ（ゴミ）を映していた。driver は表示レジスタを一切触っていないので、クロック設定は無関係。

## 修正
`src/drivers/gpu/i915/ggtt.c`:
- boot handoff `kern_boot_handoff("pcat.framebuffer")` で framebuffer の物理範囲を取得。
- BAR2（`GEN4_GMADR_BAR`）のアパーチャ base を読み、`framebuffer.physical_base - aperture.base` = GGTT オフセット
  （アパーチャオフセットは GGTT オフセットと 1:1）から保持すべき GGTT ページ範囲を算出。
- scratch fill でその範囲だけ書き換えをスキップし、bitmap で予約（GEM が再利用しない）。
- framebuffer がアパーチャ外なら安全に no-op（従来動作）＋診断ログ。表示レジスタは触らない。

新規 static `i915_ggtt_boot_scanout()`。範囲はテーブルサイズにクランプ。

## 検証（agent-1）
- host fixture 全 PASS（uncore/gtt/irq/lrc/stream/backend、通常＋ASan/UBSan）。
  gtt に新規 `test_scanout_preserved` を追加（sentinel PTE が保持され、両隣は scratch、予約で allocator が範囲を飛ばす）。
- 静的解析 gcc -fanalyzer / clang --analyze: 0 件。
- kernel build（config-i915-amd64.mk）PASS、amd64 vmunix check PASS。native disk-image 生成 PASS。

## 未検証（ユーザー担当）
実機ネイティブ起動での目視確認。native disk-image: `build/i915-ggtt-check/hdd-image.img`（242MB）。
起動時 dmesg に `i915: preserving N GGTT scanout pages from page M` が出れば保持成功。
`scanout not preserved` のログが出た場合は framebuffer がアパーチャ外（別経路）なので追加対応が必要。
