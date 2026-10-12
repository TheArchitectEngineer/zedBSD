<!-- awesome-plan project=zedbsd record=ws141-p018 -->

# ws141-p018: 部分の vkCmdCopyBufferToImage の費用（CPU の行の写し、または GPU の部分の tile pass）

Parent: [WS141](../ws.md)
Status: planning（候補の記録。2026-10-12 P3。順は p016 の (b)・WS211 p003・Pi の計測の後。方式は Pi の perf の計測で決める）
Phase disposition: normal
Queue: なし（候補。Q1 の判断の後）

## 由来

- [WS210 p013 の E7](../../ws210/phase013/phase.md)（2026-10-12、Q1 経由）: Keiland の app の present を、部分の `vkCmdCopyBufferToImage` にする。そうすると bcm2711 の kernel が、その部分を CPU で行ごとに写す。部分の copy の kernel の CPU の費用を、消すか減らすことが求められている。
- Q1（2026-10-12）: 「今は (b)・p003・計測の順のまま、候補として phase に記録して」。

## 今の経路（source で確かめた事実）

- 画像の全体を覆う 1 つの region の buffer→image の copy だけが、GPU の tile の copy pass になる（`vulkan-native-image.c:653-722` の `buffer_image_whole`、`vulkan-native-job.c` の COPY_BUFFER_TO_IMAGE）。
- それ以外の部分の copy は ENOTSUP で CPU の経路（`bcm2711_vulkan_buffer_copy_run`）に回る。`vulkan-buffer-copy.c:262-273` で、行ごとに `kern_memcpy` を使い、行の数だけ写す。
- `kern_memcpy`（`src/kern/kcrt.c:114-145`）は、両方の address が 8 byte に揃う時だけ 8 byte の word で写し、他は 1 byte ずつ写す。写し先と写し元は uncached（Normal NC）の memory。揃わない copy は、1 byte の uncached の load と store になって遅い。
- この copy は executor の step の中、つまり controller の mutex の下で走る（perf の `xfer` に数える）。

## 案

- **(a) CPU の写しを速くする**: buffer-copy の行の写しを、uncached に向けた 64 byte 単位の写しにする（ldp/stp。揃わない先頭と末尾だけを小さく写す）。kernel の build は `-mgeneral-regs-only` なので、汎用 register の ldp/stp を使う。
  - 置き場所は 2 つ考えられる。1 つは `kern_memcpy` 自体（src/kern で、hal.h は不変。全体に効くので Q1 の判断が要る）。もう 1 つは bcm2711 の driver の中の helper（WS141 の範囲）。
  - 揃わない時の 1 byte の経路は、8 byte の unaligned の load と揃った store（AArch64 は Normal memory の unaligned を許す）にする。
- **(b) GPU の部分の tile pass**: `buffer_image_whole` を広げて、region を覆う 64×64 の tile だけを load と store する tile の copy pass にする。staging（buffer）は常に frame の全体が正しいので、tile 単位で丸ごと写して正しい。
  - CPU は写さない。GPU は tile の数に比例する。
  - 部分の更新は write ring の箱に記すので、後の snapshot の部分の変換も効く。
  - mutex の下の CPU の時間は、(a) よりさらに小さい見込み。
  - 費用は、copy pass の準備（prep・alloc）と、GPU の待ち。

## 決め方

- Pi の perf（v3d.perf=1、place9 の後の計測の boot）で、app の hover の部分の present の 1 回について、次を測る。
  - `xfer`（CPU の copy）、`prep`、`exec`、`copy` の tile の数、`hold_us` の step、入力→shown。
- (a) の 64 byte の写しの速さは、host（aarch64 の cross build の小さな試験）と Pi で測る。uncached の帯域で、1 byte・8 byte・64 byte を比べる。
- 速く、引っかかりが無い方を採る（WS211 のユーザーの基準と同じ: Pi で少しでも引っかかりがあるなら許容しない）。両方を入れることもあり得る（小さな箱は (a)、大きな箱は (b)）。

## 範囲と順

- WS141 の kernel の driver の中で扱う（(a) で `kern_memcpy` を変える時は Q1 の判断）。UAPI と hal.h は変えない。
- 順: ws141-p016 の (b) → WS211 p003 → Pi の計測 → この Phase（Q1 が Queue に入れた時）。入る時は、設計を先に Q1 に出して review に回す。
