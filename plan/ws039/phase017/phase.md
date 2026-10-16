# ws039-p017 VRAM の 256 MiB の窓の固定の解除

- 親: [WS039](../ws.md)、状態: planned（Queue 未割当）
- 起点（2026-10-16 ユーザー）:「glassが解決したら、ファン制御をマージして、そのあと窓の固定は解除して自由に使えるように改善しましょう。」（§15 #7「まず256MBで実装します。」の後の段）
- 順: p009 の glass の修正 → p012（SMU の Tier F、fan の制御）の実機と merge → この段。
- 範囲（案、着手の時に design.md 5.5 で詳しく）: GPU だけが触る BO（render target・offscreen・texture 等）を BAR0 の窓の外の VRAM（6 GiB の全体）に置く、firmware が Re-Size BAR で広げた BAR0 は全域を使う、窓が足りない時の置き場の fallback。Terminal などの app が起動しない件（2026-10-16 の観察）が窓の不足なら、ここで解く。
- 実機: 10.0.10.3 で desktop と複数の app（Terminal 等）が起動する。
