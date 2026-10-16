# ws039-p017 VRAM の 256 MiB の窓の固定の解除

- 親: [WS039](../ws.md)、状態: planned（Queue 未割当）
- 起点（2026-10-16 ユーザー）:「glassが解決したら、ファン制御をマージして、そのあと窓の固定は解除して自由に使えるように改善しましょう。」（§15 #7「まず256MBで実装します。」の後の段）
- 順: p009 の glass の修正 → p012（SMU の Tier F、fan の制御）の実機と merge → この段。
- 範囲（案、着手の時に design.md 5.5 で詳しく）: GPU だけが触る BO（render target・offscreen・texture 等）を BAR0 の窓の外の VRAM（6 GiB の全体）に置く、firmware が Re-Size BAR で広げた BAR0 は全域を使う、窓が足りない時の置き場の fallback。Terminal などの app が起動しない件（2026-10-16 の観察）が窓の不足なら、ここで解く。
- 実機: 10.0.10.3 で desktop と複数の app（Terminal 等）が起動する。

## 2026-10-16 の調べ（P3、p009 の中）

- app が起動しない理由は 256 MiB の窓: `amdrdnactl status` の cpu_free=25MiB（gpu_free=5862MiB は余る）。Music の起動で `KWL IMPORT … bytes=2961408` の後 `MUSIC FAILED operation=window error=5`（ENOMEM が EIO として app に返る）。
- executor の memory type が 1 つ（DEVICE_LOCAL|HOST_VISIBLE|COHERENT）で、blob・内部の確保が全部 CPU の窓の pool から取られる。
- この段の案: (1) memory type を 2 つに（DEVICE_LOCAL だけ = 窓の外、と窓）、CPU が触らない image・offscreen・swapchain は窓の外へ。node の blob も MAPPABLE が無い時は窓の外。libvulkan の memory.c は host-visible の type の時だけ MAPPABLE を付ける（Q1 が許可、i915・nvrtx・venus の振る舞いが変わらないことを確かめる）。(2) display の frame 2 枚を storage の present が来た時だけ確保。UEFI の Re-Size BAR の設定には頼らない。
