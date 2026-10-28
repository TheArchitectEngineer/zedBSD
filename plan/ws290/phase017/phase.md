<!-- awesome-plan project=zedbsd record=ws290-p017 -->

# ws290-p017 boot framebuffer の使用終了の通知と解放

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §13（版 4.15 の「p012 の実装」の boot framebuffer）と、この phase.md の設計の節（着手で書く）。
- 状態: planned
- 依存: p012（表示）、p011（local の allocator と `ghci_memory_boot_release`）

## ユーザーの決定（2026-10-28、原文は ws.md）

「GPUの使用開始は、consoleドライバに通知しています。consoleドライバは、/dev/gprahicsドライバを使ってGOPに描画していると思います。そこで、/dev/graphicsデバイスに使用終了の口を作るのがいいと思います。GPUの使用開始時に、/dev/consoleに使用終了を通知、/dev/consoleが/dev/graphicsに使用終了を通知、/dev/graphicsがカーネル内APIを呼んでマッピング解除を要求。カーネル内APIがHALを呼んでマッピングを実際に解除。ここまではWS290で実装。将来的に、Keilandが終了してGPU使用を終了するときに、/dev/gpu0のscanoutを/dev/consoleのバックエンドに切り替える機能を作るのがいいです。後者は今回はやらず、future workにすればいいです。」

解釈（Q1 の確かめにユーザーが「その理解でよい」）: GPU の使用開始の時に kernel の text-display（`kern_text_*`、console）へ「使用終了」を通知 → text-display が graphics の backend（amd64 は `src/drivers/platform/pcat/graphics/pcat-graphics.c`、他の platform の backend も同じ口）へ通知 → backend が kernel 内 API で framebuffer の mapping の解除を要求 → kernel 内 API が HAL で実際に解除。panic の表示は v1 で無し（serial と klog）。scanout を console の backend に戻す機能と panic の hook は Future Work。

## 範囲

1. 設計（code の前、design-reviewer の review を通す）: 今の `kern_text_suspend` の呼び手（amdrdna・i915・powervr など）との関係（suspend と「使用終了」の違い）、他の GPU driver が使わない時に今の振る舞いが変わらない事、`/dev/graphics` の firmware の backend の扱い（使用終了の後の open・mmap・描画）、kernel 内 API と HAL の呼び方（HAL の API の宣言・契約を変える要があれば差分の案を Q1 へ、承認が要る）、GHCI の driver の呼ぶ時（最初の自分の flip が取られた後、lock を持たずに）と `ghci_memory_boot_release` の条件（`boot_console` を「使用終了の通知が成功した」に置き換える）。
2. 実装: kernel の text-display、platform の graphics の backend（amd64 の pcat と、同じ口を持つ他の platform の backend）、kernel 内 API と HAL の呼び出し、GHCI の driver。
3. 試験: host 試験（GHCI の場面: 通知が成功した時だけ解放、失敗・未対応の時は解放しない）と、kernel の側の試験（ある物の範囲で）。amd64・riscv64（GHCI 有り・無し）の build の warning 0。

## 受け入れ

- 設計の review の H が 0。
- 場面 PASS、build の warning 0。他の GPU driver（amdrdna・i915・powervr）の使い方で振る舞いが変わらない事を build と既存の試験で確かめる。
- 実機（GOP の framebuffer の解除の後に console が描かない事）は p014 か Q1 の割当の QEMU で。
- 見積り: 14 LW。

## 記録

- 2026-10-28 P3（q1644-i01）: ユーザーの決定（上）で立てた。
