<!-- awesome-plan project=zedbsd record=ws141-p017 -->

# ws141-p017: bcm2711 の Vulkan の stencil（Notes の winding の stencil で塗る線）

Parent: [WS141](../ws.md)
Status: planning（調べと見積もり。2026-10-12 P3。ws141-p016 の (b) の後の候補で、Queue には入れていない）
Phase disposition: normal
Queue: なし（候補。Q1 の判断の後）

## 由来

- 2026-10-12 Q1: Notes は stencil を使う（`userland/desktop/notes/render.c` の NOTES_PIPE_STENCIL・FRINGE・COVER と、depth-stencil の attachment）。bcm2711 の `vulkan-pipeline-state.c:38-40` は `pDepthStencilState != NULL` を無条件に ENOTSUP にするので、Pi では Notes はまだ起動しない見込み。その調べと見積もりを、(b) の後の候補として書く。

## Notes が要るもの（source で確かめた事実）

- format: `VK_FORMAT_D24_UNORM_S8_UINT` → `D32_SFLOAT_S8_UINT` → `S8_UINT` の順に、optimal tiling の `DEPTH_STENCIL_ATTACHMENT` の feature がある最初の物を選ぶ（render.c:862-893）。どれも無いと Notes は始まらない。
- stencil の image: 窓の大きさの 1 枚と、ページの絵の 1 枚。usage は `DEPTH_STENCIL_ATTACHMENT` だけ（render.c:912-970）。
- render pass: 2 つの attachment（colour と depth-stencil）。stencil は `stencilLoadOp = CLEAR`、`stencilStoreOp = DONT_CARE`（render.c:1011-1024、1092-1111）。stencil は tile buffer の中だけで足り、memory への load と store は要らない。
- pipeline（render.c:1871-1923）:
  - stencil の pass: `stencilTestEnable`、compare ALWAYS、front の pass は `INCREMENT_AND_WRAP`、back の pass は `DECREMENT_AND_WRAP`（両面）、`colorWriteMask = 0`。
  - fringe の pass: compare EQUAL（ref 0）、writeMask 0。
  - cover の pass: compare NOT_EQUAL、pass で ZERO。
  - 全部 `cullMode NONE`、`frontFace CCW`、depth test は使わない、blend は premultiplied（ONE / ONE_MINUS_SRC_ALPHA）。

## bcm2711 で今断っている所

1. format の性質: `vulkan-query.c` の `format_properties` は depth-stencil の format に attachment の feature を出していない（推測。実装の時に確かめる）。
2. image: depth-stencil の usage と format の作成・大きさ・layout。
3. render pass と framebuffer: `vulkan-target.c:259-280` は colour の attachment だけを受け、stencil の op は DONT_CARE 以外を ENOTSUP にする。attachment は 1 つの前提。
4. pipeline: `vulkan-pipeline-state.c:38-40`（`pDepthStencilState`）と、`vulkan-pipeline-state.c:228`（colorWriteMask は全部の時だけ）。`vulkan-pipeline-decode.c:258-` の raster の decode も depth-stencil の attachment を断る。
5. barrier: `vulkan-barrier-validate.c:155` は depth/stencil の layout の遷移を受けない。
6. native の CL と RCL:
   - `native-bin.c:115` は CFG_BITS で stencil を切ったまま。
   - STENCIL_CFG の packet（front と back）を出していない。
   - RCL は tile buffer の depth/stencil を「使わない clear」にしている（`cl.c:78`、`native-pass.c:490`）。
   - colour の write mask を 0 にする手段（TLB の write mask、または blend の state）が無い。

## 直しの形（案）

- **stencil は tile buffer の中だけ**（Notes の store が DONT_CARE なので）: RCL の tile の depth/stencil の internal type を D24S8 にし、stencil の clear 値（0）を毎 tile で clear する。memory の load と store は出さない。stencil の image は、作成と大きさの計算だけを持ち、GPU からは使わない（backing は確保しても読み書きしない）。
- **pipeline**: depth-stencil の state を decode して pipeline に持つ。対応するのは次の物で、それ以外は ENOTSUP。
  - stencilTestEnable、compare（ALWAYS・EQUAL・NOT_EQUAL・NEVER・LESS 等の 8 種）、op（KEEP・ZERO・REPLACE・INCR/DECR_AND_CLAMP/WRAP・INVERT）、compareMask・writeMask・reference。
  - 両面（front と back）。depth test と depth write は off だけ。
- **CL**: draw の前に、pipeline の stencil の state から CFG_BITS（stencil enable）と STENCIL_CFG（front と back の 2 つ）を出す。Mesa の v3dv の `pack_stencil_cfg` と、V3D 4.2 の packet の定義を正本にする（版と hash を記録する）。両面の判定には、raster の frontFace と cullMode NONE を使う。
- **colour の write mask 0**: TLB の colour の write mask（RCL か CL の per-RT の write mask の field）か、blend の state で dst を保つ形で実装する。正本で field を確かめる。
- **render pass と framebuffer**: colour 1 枚と depth-stencil 1 枚の 2 つの attachment を受ける。depth-stencil の loadOp・stencilLoadOp は CLEAR か DONT_CARE、store は DONT_CARE だけ（LOAD と STORE は ENOTSUP。将来の拡張）。
- **format**: `S8_UINT` か `D24_UNORM_S8_UINT` の一方に、optimal tiling の DEPTH_STENCIL_ATTACHMENT を出す。D24S8 を推す（V3D の tile buffer の native な形）。
- **barrier**: depth-stencil の layout（UNDEFINED → DEPTH_STENCIL_ATTACHMENT_OPTIMAL など）の遷移を受ける（tile buffer の中だけなので、実際の同期は要らない）。

## 見積もり

| 部分 | 規模（推測） |
| --- | --- |
| 正本の調べ（V3D 4.2 の STENCIL_CFG・CFG_BITS・RCL の Z/S の field、Mesa v3dv の packing） | 小 |
| format の性質・image の作成・barrier | 小（100〜150 行） |
| render pass と framebuffer の 2 つの attachment（decode・検証・保持） | 中（150〜250 行） |
| pipeline の depth-stencil の state の decode と保持、colorWriteMask 0 | 中（150〜250 行） |
| CL の CFG_BITS・STENCIL_CFG、RCL の Z/S の clear、write mask | 中（150〜300 行） |
| host 試験（packet の bit を Mesa の pack 関数と照合、decode の受け入れと拒否、RCL の clear） | 中（200〜300 行） |
| Pi の確かめ（Notes で線を引く、自己交差・重なり・消しゴム） | 実機 1 回 |

- 合計で 750〜1250 行、設計と review、実装 2〜3 段、Pi の確かめ。UAPI と hal.h は変えない見込み（wire は既存の Vulkan の構造体をそのまま運ぶ）。
- 危険: tile buffer の stencil の初期化と、両面の判定の向き（V3D の front の定義と Vulkan の frontFace の合わせ）。Pi で、Notes の自己交差する線で確かめる。

## 代わりの案（WS141 の外）

- Notes の側で stencil を使わずに塗る。winding を R8 か R16 の offscreen の colour に加算の blend で数え、cover の pass でそれを sample する。Pi が受ける blend は今は ONE / ONE_MINUS_SRC_ALPHA だけなので、加算の blend（ONE / ONE）を Pi の driver が受けるかを先に確かめる。受けるなら、kernel の変更なしで Notes が動く。Notes の WS の担当と Q1 の判断になる。

## 次

- (b) の後に、Q1 が Queue に入れるか決める。入れる時は、正本の調べと設計を書いて design-reviewer の review に出す。
