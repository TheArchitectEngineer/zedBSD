<!-- awesome-plan project=zedbsd record=ws244-p003 -->

# ws244-p003 intel/ の GT の header・表

Status: in-progress（2026-10-18 q1251-i01、P1。途中で中断、WS260 を先にするユーザーの決定）

- 親: [WS244](../ws.md)。範囲: gt-regs.h・commands.h・gt-power.h・lrc-offsets.h・mocs.h・mocs-table*.inc・workarounds.h・mcr-ranges.inc・engine-table*.inc・forcewake-ranges*.inc・pci-ids.h と、それを使う file。

## 実行の記録（2026-10-18）

### 段 1: Linux の名前の macro の改名（済み、commit は下）

- gt-regs.h・gt-power.h・mocs.h・workarounds.h の Linux の名前の macro に `I915_GT_`、commands.h に `I915_CMD_`（`__INSTR` → `I915_CMD_INSTR`）、pci-ids.h の `INTEL_*_IDS` を `I915_PCI_*_IDS` にした。全部で 521 の名前、対応は [rename-map.txt](rename-map.txt)。lrc-offsets.h は既に `I915_LRC_*`。既存の名前との衝突は無い（src・include・userland を grep）。
- 置き換えは src/drivers/gpu/i915 と include の .c・.h・.inc の識別子だけ。`Values referenced:`・`Procedure referenced:` の block（Linux の名前を引く所）と文字列の literal（log の文言）は変えていない。行は増減していない。
- 動作の確かめ: 置き換えの前と後で、amd64 の既定と I915_TESTS=y の build の i915 の object（124 個と 535 個）の SHA-256 を比べた。device.o 以外はすべて一致した。device.o は bitcode を IR にして比べると、違うのは inline asm の srcloc の metadata（前処理後の文字の位置）の 2 つだけで、code は同じ。GT の register の書き込みの列は object が同じなので変わらない（mmio-record の比較の代わり）。
- build: 両方の組で rc=0、warning 0、`amd64 vmunix check: PASS`。

### 再開の手順（段 2、未着手）

1. 各 header の先頭の外部の Copyright・MIT の許諾・「Modified for zedBSD … Sources (SHA-256 …)」・Rewrites の行を外し、zedBSD の表示（Zlib）と役割の説明だけにする（p002 の display-regs-gen9.h が手本）。
2. block ごとに `Values referenced: Linux <版> <path> (<Linux の名前>)` を付ける。gt-regs.h・commands.h・lrc-offsets.h・mocs.h・pci-ids.h の値は Linux v6.19 から取ったと先頭に書かれているが、手元には 6.8.12 の tarball しか無い。6.8.12 で同じ値なら v6.8.12 を、無ければ v6.19 と書いて phase.md に「未確認」と残す。Linux の名前は rename-map.txt の左の列。
3. .inc の表（mocs-table・engine-table・forcewake-ranges・mcr-ranges）も同じ。engine-table の行の形が Linux の struct の写しかを見る（inventory の要確認）。
4. 改名で長くなった `#define` の列のそろえは直していない（必要なら段 2 で）。
5. build 2 組（amd64 既定、I915_TESTS=y）warning 0。comment だけなので object の比較は行の位置で変わる（__LINE__）ので行わない。
