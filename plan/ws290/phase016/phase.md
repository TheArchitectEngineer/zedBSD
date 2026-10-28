<!-- awesome-plan project=zedbsd record=ws290-p016 -->

# ws290-p016 WaitEvents の保留（R10 の (b)）

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §11.4 の「WaitEvents（R10・Q13）」の (b) と「WaitEvents の v1（版 4.13）」。
- 状態: in-progress
- 依存: p009（WaitEvents の (a) と kernel の待ちの語）、p012（表示の ring の使い方が決まった後）

## 範囲

同じ submission の前に SetEvent の無い WaitEvents（host の vkSetEvent や前の submission の GPU の SetEvent を待つ物）を、v1 の `ENOTSUP` の代わりに実装する: submit をその WaitEvents で切り、残りとその client の後の submission 全部（submission の順を保つ）を kernel が保留し、event の語が 1 になった時（host の vkSetEvent、前の submission の SetEvent の fence の IRQ ごとの読み）に続きを置く。保留は reset で DEAD になった session と executor の close で ERROR で完了する。ring に client が書ける語を待つ `WAIT_FENCE` を置かない（p009 の review の H2）。

## 受け入れ

- 場面 PASS: host の SetEvent で続く、前の submission の SetEvent で続く、保留の間の後の submission の順、ResetEvent・close・reset で保留が ERROR、保留の間も他の session と表示が止まらない。
- host 試験と amd64・riscv64 の build（warning 0）。実機は p014。
- 見積り: 10 LW。

## 記録

- 2026-10-28 P3（q1644-i01）: ユーザーの答え（2026-10-28、Q1 経由のクリック）「WS290 の中で後の Phase」で立てた。v1（p009）は `ENOTSUP` で断る。
- 2026-10-28 P3 の helper（q1644-i01）途中: 保留を実装した（build 前の host の compile は通る）。`vk-execute.c` は WaitEvents の同じ submission で set されない event を cut（render pass の中なら pass の begin の位置）にし、render pass の外の command の間に 256 dword ごとの印の cut を置く。`vk-queue.c` は cut のある submission と保留の後の submission を device ごとの `ghci_vk_hold` の列に置き、cut の event の語が 1 で書く仕事が無くなった所まで片（piece）で置く（host の vkSetEvent は client の thread で、GPU の SetEvent は service の round で `hold_lock` の trylock と空いた ring だけに）。job marker・frame は `GHCI_VK_HELD_GENERATION` の ticket で待ち、置いた時に本当の ticket を受ける。保留は stop・isolate・close・device の破棄・GPU の喪失・reset で終わった device の仕事で ERROR。試験はこれから。
- 2026-10-28 P3 の helper 途中: `ghci-execute-host-test.c` に場面 hold・hold fail・hold pieces を足し、queries・events の v1 の期待（ENOTSUP）を保留に直した。execute 234/0（scratch の build で、全体の script はこれから）。
- 2026-10-28 P3 の helper（q1644-i01）結果（commit は P3）:
  - **実装**: `vk-execute.c` — WaitEvents の event のうち同じ submission で前に set（reset の後でない）された物は従来どおり私的な語の `WAIT_FENCE`、それ以外（host・前の submission の GPU が set する物、この submission が reset した物）はその位置（render pass の中なら pass の begin の位置）の cut の待ちにする（v1 の `ENOTSUP` は無くなった）。render pass の外の command の間に印だけの cut（4096 dword ごと、ring の 1/8 以下）。cut と render pass の begin の後は command buffer の bind を全部置き直す（片の間の reset や driver の clear の program で session の bind が失われても正しいように）。`vk-queue.c` — cut に待ちのある submission と、device が保留を持つ間の後の submission は全部 `ghci_vk_hold` として device の列に順に置き、`ghci_vk_holds_advance` が cut の event の語が 1 で、その event を書く置いた仕事が無くなった（自分の未配置の部分が cut の前で書く時は先にそこまで置いてその ticket を待つ）所まで片で置く。各片は bind・（先頭でなければ）head の barrier・packets・（末尾でなければ）tail の barrier・fence。client の thread（host の vkSetEvent・vkQueueSubmit）は gate を通って ring の空きを待ってよい。service の thread は `ghci_vk_service` の始めに session の `hold_lock` を trylock し、空いた ring に今入る片（ring の半分まで）だけを `ghci_cp_try_enter` で置く（cp.c は変えていない）。`hold_lock` は frame ごとに `ghci_vk_stream_execute` が取る。job marker・context fence の frame は保留中は `GHCI_VK_HELD_GENERATION` の ticket（seq = 最後の保留の serial）で待ち、その保留を置き終えた時に本当の ticket を受け、失敗では FAILED。VkFence は held・lost を持ち、置き終えて ticket、失敗で DEVICE_LOST。保留は session の stop・isolate・drain（close）・vkDestroyDevice・GPU の喪失・device の最後の置いた仕事が reset で FAILED（その GHCI session が DEAD）で ERROR。host の ResetEvent は語を 0 にするだけで、保留は待ち続ける。保留の host の bytes は session の budget（64 MiB）から取る（超えれば OUT_OF_HOST_MEMORY）。ring に client の書ける語の `WAIT_FENCE` は置かない。
  - **試験**（`ghci-execute-host-test.c`）: 場面 hold（host の set で続く・保留の間の後の submission は順に置かれる・host の reset の後も待つ・fence・marker・frame は走った後に完了・stop poll は待つ・前の submission の GPU の SetEvent が走るまで保留し service が残りを置く・他の session の submission が保留の間に走り ring は WAIT_FENCE で止まらない・close で保留と marker が EIO）、hold fail（置いた仕事の fault → reset で保留が失敗、fence DEVICE_LOST・marker EIO・保留の fill は走らない、isolate で保留と marker が EIO）、hold pieces（4 KiB の ring で 400 fill の保留を service が片で順に置き各片に head・tail の barrier、render pass の中の wait は pass の begin から保留）。queries・events の v1 の期待（ENOTSUP）を保留の期待に直した。
  - **確かめた事**: `sh plan/ws290/tests/ghci-host-test.sh` rc=0（words 141/0、ref 56/0、model 271/0、driver 1019/0、old-names・isa PASS、gsi1 50/0、vk 438/0、pipeline 175/0、execute 234/0、meta 98/0）。`make ZEDBSD_CONFIG=plan/ws290/tests/config-amd64-ghci.mk BUILD=build/ws290-p016-amd64 build/ws290-p016-amd64/vmunix` rc=0・warning 0（amd64 vmunix check PASS）、`config-visionfive2-ghci.mk` → `build/ws290-p016-vf2/vmunix` rc=0・warning 0。
  - **未実施・残り**: 表示が止まらない事は「ring が WAIT_FENCE で止まらず他の session が走る」で確かめ、表示の flip そのものの場面は作っていない。budget の上限の場面は作っていない（code の確認だけ）。2 つの cut の間が ring の半分を越える片（巨大な render pass）は service では置けず、次の client の thread（host の vkSetEvent・同じ device の vkQueueSubmit）か close まで待つ。Vulkan が禁じる「保留中の仕事が使う object の破棄」は device の最後の置いた ticket で heap を返すので、その client の session の中だけで壊れうる。design.md §11.4 の「WaitEvents の v1（版 4.13）」の ENOTSUP の記述と ghci-host-test.sh の既定の `GHCI_SPEC_REV`（d9252e4）は P3 の持ち物で直していない。実機は p014。
