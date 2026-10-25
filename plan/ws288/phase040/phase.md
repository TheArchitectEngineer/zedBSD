<!-- awesome-plan project=zedbsd record=ws288-p040 -->

# ws288-p040 GHCI の memory の置き場所の設計どおりの実装（host の ring・fence、BAR2 は frame buffer だけ、窓を閉じる bit）

- 親: [WS288](../ws.md)。関係: [p039](../phase039/phase.md)（GPU → host の経路、HOST_LIMIT）、[p006](../phase006/phase.md)（driver）、[p013](../phase013/phase.md)。
- 状態: in-progress（2026-10-25 17:57 Q1、q1581 P2）
- 依存: p039（575dcf6、T1-801 で試験中。main に入る前は p1-p039 の上で作り、後で main に合わせる）。

## 決定（2026-10-25 17:57 ユーザー）

Q1 の提案（GHCI の MEM の capability に window の enable の bit を足し driver が自分の scanout に切り替えた後に閉じる、console と panic の画面は BAR2 に頼らず GPU の driver の scanout の経路（host の memory の buffer）を使う、driver の置き場所を GHCI §11.3 どおりに）の後、「では、この設計でGHCI仕様をアップデートして、zedBSDのドライバを仕様に合わせて変更し、RTLも必要に合わせて修正しましょう。」

先の決定: ring・fence は常に host memory、BAR2 は frame buffer 専用・32 bit か 64 bit（amaterrace main 8533ca6）。GOP の frame buffer を引き継がない場合は窓を閉じてよい。console は graphical session の後は使わない、使うなら /dev/gpu の scanout と Vulkan の display の上。

## 範囲

1. **GHCI**（amaterrace docs/ghci.md）: MEM の capability に window の enable の bit（host が書ける、reset で 1 = GOP などがそのまま使える、0 で BAR2 への host の access は捨て・読みは 0）。§11.1 に「窓は local memory の見え方で専用の memory ではない、driver が引き継いだ後は普通の local memory、閉じてもよい」。GOP などの起動の frame buffer の引き継ぎの規則（attach で DC の register から今の scanout を読める、最初の flip の TAKEN の後に範囲を返す）を §11 か §13 に短く。
2. **RTL**: MEM の capability の register（atg_cp_regs）と、KC705 の BAR2 の経路（atg_kc705_pcie と atg_kc705_mig の BAR2 の口）で enable を見て、0 なら書きを捨て読みは 0 を返す（AXI の応答は OKAY のまま）。TB。算術は am_ の module。
3. **zedBSD の driver**（src/drivers/gpu/ghci/）: memory.c の kind_for を GHCI §11.3 どおりに: device-local は local（窓の外を優先）、host-visible（staging・host が書く uniform・readback）と ring・command buffer・fence は host memory、host の buffer は local の外かつ HOST_LIMIT（MEM +0xC）の下、aperture は local と host の両方を覆う。scanout の buffer は local。BAR2 は driver が自分の scanout に切り替えた後（または窓を使わない時）に enable を 0 に。Vulkan の heap は discrete で 2 つ（device-local と host-visible・coherent）。vk/ と firmware の upload・readback が copy の command で通ることを確かめ、無ければ足す。host 試験（ghci-host-test.sh）と e2e（ghci-e2e.sh: Verilator の model は KC705 の形でないので、LOCAL と HOST の両方を持つ形にするか、どの形で確かめるかを設計の節で決める）。
4. 範囲の外: GOP の frame buffer の実際の引き継ぎ（KC705 に option ROM が無い）、panic の画面の経路（後の Phase）、起動の frame buffer の console の driver からの取り上げの GPU の core の仕組み（後）。

## 受け入れ

GHCI の文、RTL の TB（窓を閉じると BAR2 の書きが捨てられ読みが 0）、driver の host 試験と vmunix の warning 0、e2e が新しい置き場所（ring・fence が host）で PASS・oracle mismatch 0。make sim・Verilator は T。

## 記録

- 2026-10-25 17:57 Q1: 新設、P2 に（amaterrace の worktree p2-p040 は p1-p039 575dcf6 から、zedBSD の worktree は新しく）。
