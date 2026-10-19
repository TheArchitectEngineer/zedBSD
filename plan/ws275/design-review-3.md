<!-- design-reviewer の差分の確認（2026-10-19、ws275 design.md 改訂 3）。Q1 が報告の本文をそのまま置いた -->

## WS275 p001 改訂 3 の差分の確認（design.md・proposed/hal-sun4u-names.md）

読むだけで確認しました。source と plan は変えておらず、build・QEMU・commit・rm もしていません。`.internal/` は読んでいません。使ったのは grep・sed・cat と、`python3 -I` で wallpaper の PPM の画素を読む処理です。

### 結論
- Blocker はありません。
- Major が新しく 1 つあります（N-1、§5 の判定の手順）。
- 前回の M-1〜M-6 は、どれも本体は直っています。値と計算は source と合いました。

### Major

**N-1. §5 の判定の手順が、実際の image では通りません（design §5 l.222-251、ws.md の完了の条件 2）**

改訂 3 の m-8 と M-4 への直しで入った誤りで、次の 4 点です。
- **(a) `tr` が無い**: `plan/tools/ports/config-sun4u.mk:56` の `ZEDBSD_USER_PROGRAMS` は `cat chown echo login ls mkdir sh uname` だけです。設計の試験の config はこれに X の 4 つを足すだけなので、`echo ws275-ok | tr a-z A-Z` は `tr: not found` になり、`^WS275-OK$` は出ません。
- **(b) 行末の `$` が合わない**: zwm は 1024x768 の root に `/usr/share/zwm/backgrounds/1024x768.ppm` を描きます（`userland/x11/zwm/main.c:395-404,493-494`。Makefile が zwm を選ぶと image に入れます）。
  - boot-test.py の `read_cells` は画面の幅いっぱいまで cell を読みます（`plan/tools/boot-test.py:203-220`）。
  - zterm は幅 984（`zterm/main.c:323`）なので、x≈1006 から右は wallpaper です。PPM から計算すると、zterm の 43 行のどの行でも x 1008〜1021 に点灯の画素（r+g+b≥384）があります。
  - そのため各行の末尾に記号か「�」が付き、`^WS275-OK$` も、prompt の正規表現の `$` も合いません。
- **(c) 色の基準の点が root の色ではない**: (1016,400) の画素は 0x203040 ではなく wallpaper の **0xff7fff** です。R と B が同じ値なので、R と B の入れ替わりもこの点では見えません。
- **(d) `keys:<Enter>` の書き方**: `keys:TEXT` は文字列そのものを打ってから return を押します（`boot-test.py:124-142`）。`<` は表にあるので、shell には `WS275-OK<Enter>` が入って構文の誤りになります。

修正案（どれも HAL の文面には触れません）:
- 打つ文字は `echo ws275-''ok`、期待は `^ws275-ok(\s|$)` のようにします。
- Enter だけ送るときは `keys:`（空）にします。
- 色の基準は zshell の bar（0x17212b）にします。2 つ目の点は wallpaper の PPM から、R と B が違う点（例 0xff7f00）を選び、期待値はその PPM から取ります。

### Minor
- **m-A. 行の分割と pattern**（§4.1）
  - RGB24 の chunk の 1365 は 8 の倍数ではありません。`pattern_bit` は image の中の x で位相を決めるので（`pcat/graphics/backend.c:435,1025-1033`）、`BLIT_PATTERN` で RGB24 の幅が 1366 以上だと、2 つ目の chunk で位相が 5 ずれます。1360 にするか、位相を backend に渡してください。
  - pcat は 1 行ずつ height 1 で呼ぶので、BLIT_PATTERN の y の位相が常に 0 です。これは既存の欠陥です。generic の口では、pattern の原点を request からの相対の (x, row) と定義してください。pcat の直しは本 WS の外で、Bug の候補です。
  - p003 の host 試験に、pattern 付きの RGB24 の幅 1366 を足してください。
- **m-B. 分割の不変条件**（§4.1 l.73-74）
  - 「packed を row_buffer より大きくしない」は条件として不正確です。正しくは「chunk ≤ min(8×sizeof packed, sizeof row_buffer)」で、static assert にしてください。
  - 512 byte の `packed` は stack ではなく、`row_buffer` と同じ `.vfs_bss` に置くことを勧めます。
  - UAPI に「行の頭の bit の offset」はありません（`include/uapi/graphics.h:100-114`）。その文は削ってください。
- **m-C. M-3 の新しい意味の帰結**（proposed l.38-65、§6）
  - 名前は「HAL が表示している framebuffer」なので、HAL の早期の console が RGB332 と XRGB8888 と任意の pitch を描けないと名前が NULL になります。すると -g x8 の試験で /dev/graphics が無くなります。§6 の framebuffer.c の行に「3 形と任意の width・pitch」を明記してください。
  - fallback には今の `display_framebuffer`・`display_registers`（`handoff.h:77-81`）が要ります。handoff 版 3 でもこれを残すことを書いてください。
  - proposed の code 例の comment「when the loader found one」は古い意味のままです。`framebuffer_shown` を両方の道で `sun4u_framebuffer_init` が立てる形にしてください。
- **m-D. 8 bpp の期待値**: QEMU の VGA の DAC が 6 bit の時、0x24 は 0x27 に、0x55 は 0x57 に見えます（OpenBIOS の `color!` の実装は推測で、未確認）。完全一致ではなく、DAC を通した値か ±4 程度の許容で比べてください。
- **m-E. comment の直し方**: `framebuffer.c:94` の palette は数値の 0xRRGGBB で、memory の並びではありません。「little-endian word, blue first」は l.251 にだけ当てはまるので、l.94 は「0xRRGGBB values」にしてください。
- **m-F. 古い記録**
  - design §4.5・§11 と proposed l.67 は、D4 を「p002 の前に聞く」、HAL の差分を「改めて示す」としています。どちらも 19:12 に決定済みです（`master.md:647`、`ws.md:13`）。
  - D6 は、ユーザーの決定では「7 つ」（`master.md:648`）ですが、§6 は 9 行です（cmain.c と framebuffer.h が増えた）。merge の前に見せる時に、この差を一言添えてください。
  - `master.md` l.652 の引用はまたずれています（今は 18:25 の行）。行番号ではなく日時で引用してください。
- **m-G. 定義文の言い回し**: boot.h の定義文の「The last」は、一応 XRGB8888 と読めます。ただ、RGB332 の前にあるので紛れます。承認済みの文面なので、他の理由で文面を変える時だけ直してください。

### 確かめて正しかった事
- **M-1**: 1365×3＝4095、packed 512 byte＝4096 bit＝row_buffer の 4096 byte（`pcat-graphics.c:30-37,460,527-541`）。試験の幅 1025・1366・4097 は、それぞれの境界を越えます。
- **M-2**: zterm に double click と中の click の貼り付けがあります（`zterm/main.c:1446-1485`、`DOUBLE_CLICK_MS` は 400、同じ cell の 2 回の押下、貼り付けは pty へ）。Xzed は座標を clamp し、加速はありません（`xzed/pointer.h:20-36`）。§8 の Xzed 側の「未確認」は外せます。
- **M-3**: fallback の値（`pci_memory_base`＋`display_framebuffer`、640x400、pitch 2560、size 1024000、qext は LE なので BGRX8888）は `framebuffer.c:26-29,127-132` と一致します。
- **M-4**: `rgb332()`（`backend.c:936`）で計算すると、0x203040 は 0x25 で (0x24,0x24,0x55)、0x17212b は 0x04 で (0x00,0x24,0x00) です。設計の値と合います。
- **M-5**: p002b を置いて p003 の依存にしています。LW の和は 16 で合います。
- **M-6**: diff は boot.h l.56-61 にそのまま当たります。memory の順の意味は、既存の RGBX・BGRX と矛盾しません。

### p001 を受け入れてよいか
N-1 を直せば受け入れてよいです。HAL の文面（承認済みの改訂 3）は変える必要が無く、直した後は差分の確認だけで足ります。
