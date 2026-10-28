<!-- awesome-plan project=zedbsd record=ws290-p011 -->

# ws290-p011 discrete

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p011（版 4.5、ghci.md cb937e6）。
- 状態: in-progress
- 依存: p005・p009・p010

## 範囲

discrete: local memory、heap 2 つ、window と boot framebuffer

詳細は design の該当の節（§4〜§16）と §14 の p011 の場面。

## 受け入れ

- p011 の場面 PASS
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 12 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19 の表のうち、この Phase の code に効く物を着手の時にここへ写す。

- d9252e4 で仕様の残りは無い。仕様が決めていない所の driver の読み（helper、仮）:
  - boot framebuffer の大きさは `FB_STRIDE × FB_SIZE.height`（§13.3 の UMA の式と同じ）、page に広げ local の範囲で切る。DISP2 が無ければ DISP（ID 1）の同じ offset（0x10・0x20・0x28・0x2C）を読む。
  - VM_FLAG_LOCAL の違反（LOCAL で local の外、LOCAL 無しで local の中）を model は kind 10 にする（N73 の「VM の flag の違反は kind 10」の読み）。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
- 2026-10-28 P3 の helper（q1644-i01、仮の扱い）: 実装の途中の記録。
  - code（1）: `memory.c` に local の allocator（LOCAL_BASE からの offset の first fit、`range.c`、1024 run）・`ghci_memory_alloc_local`（0 埋めしない、window が覆えば view）・`ghci_memory_local_start/stop`・boot framebuffer（DISP2 か DISP の CTRL.ENABLE、FB_BASE・FB_STRIDE × 高さ、local の中だけ、allocator から外す）・解放の hook `ghci_memory_boot_release`（p012 が最初の flip の後に呼ぶ）・WINDOW_CTRL（boot framebuffer を保つ間か driver が window を使う間は 1、他は 0）。`ghci_memory_free` は local を allocator に返す。`vm.c` の map は local を `READ | WRITE | LOCAL`（flat は ENOTSUP）。`ghci.c` の start が local を始め、resource の read・write・map は local を window で（覆わなければ ENOTSUP、write は最後の byte を読み戻す、map は DEVICE）。`vk-stream.c` は local の resource を stream にしない。
  - code（2）: `vk-caps.c` の memory の型（discrete は heap 0 local・heap 1 host、型 2 つ）、`vk-memory.c`（型 0 の discrete は local、export・import は host。新しい local は `ghci_vk_execute_clear`（`vk-execute.c`、FILL_BUFFER か BLT の無い時は p010 の copy program）で 0 埋めし ticket を待つ）。
  - 試験: `ghci-model.c`（FILL_BUFFER が session の mapping の先に pattern を書く、VM_MAP の LOCAL の検査、MEM の WINDOW_CTRL の電源投入の値 1）、`ghci-driver-host-test.c`（`scene_local`: boot framebuffer を allocator から外し window を開けたまま・run は framebuffer を避ける・release の後にその範囲を渡し使わない window を閉じる・VM_MAP の flag が `READ | WRITE | LOCAL` で phys が local の番地・host は LOCAL 無し・unmap の後に run が allocator に戻る・window の有る device は driver の用に開けたまま view を持つ・unified は ENOTSUP・host memory の boot framebuffer は扱わない。`scene_local_lost`: LOST の device の local の run は quarantine に入り allocator に残り、detach で返り allocator も消える）、`ghci-vk-host-test.c`（`scene_discrete`: 型 0 が local・VM_MAP の LOCAL・FILL_BUFFER（va・大きさ・0）で 0 埋めし次の byte は触らない・型 1 と export は host で FILL 無し・型 2 は断る・buffer の memoryTypeBits 3・blob の window 経由の upload・readback・map（物理 = BAR2 + offset、DEVICE）・local を超える確保は OUT_OF_DEVICE_MEMORY・free の後に allocator に戻る・window 無しは read・write・map が ENOTSUP で WINDOW_CTRL 0・BLT を落とした discrete は FILL 無しで copy program の DISPATCH。profile の memory の型の期待を discrete で 2 つに）。
- 2026-10-28 P3 の helper（q1644-i01、仮の扱い）: 結果。
  - host 試験: `sh plan/ws290/tests/ghci-host-test.sh` PASS（words 141・ref 56・model 254・driver 641（+54）・old-names・isa・gsi1 50・vk 420（+34）・pipeline 175・execute 198・meta 98、FAIL 0。出力 `build/tmp/ghci290-host.Wmj4C7`）。
  - build（warning 0、exit 0）: `config-amd64-ghci.mk` → `build/ws290-p011-amd64`、`config-visionfive2-ghci.mk` → `build/ws290-p011-vf2`、`config/ci/config-amd64.mk` → `build/ws290-p011-amd64-ci`、`config/ci/config-visionfive2.mk` → `build/ws290-p011-vf2-ci`。最後の comment の直しの後に GHCI の 2 つを差分で build し直し warning 0。`git diff --check` 0。
  - 判断（P3 の確かめを頼む）: (1) export の記録（EXPORT_MEMORY_ALLOCATE_INFO）付きの型 0 は host に取る（別の process が blob を map する。libvulkan の WSI は最初に合う型 = 型 0 を shared で取るので、local にすると copy の表示が window に頼る）。import は resource の allocation のまま。(2) 0 埋めは allocate の中で ticket を待つ（10 s、EIO → DEVICE_LOST、timeout・作れない → OUT_OF_DEVICE_MEMORY）。GPU の後の仕事は ring の順で、CPU（window の blob）は確保が返った後なので、client は 0 埋めの前を見ない。(3) WINDOW_CTRL.ENABLE は boot framebuffer を保つ間と、driver が BAR2 を map した discrete（local の read・write・map と p012 の BLT 無しの表示の写しに使う）の間 1、他は 0。(4) local の user の map は window の物理で `DRV_GPU_MAPPING_DEVICE`（WC にしない: 書いた物を doorbell の前に読み戻せないため）。kernel の write は最後の byte を読み戻す。(5) heap 0 の大きさは LOCAL_SIZE（boot framebuffer の分を引かない）。allocator は 1024 run・1 run 2 GiB まで。表が取れない・boot framebuffer を外せない時は unified に倒す（log）。(6) local の resource は vk の stream にしない（`vk-stream.c`）。(7) flat の discrete は local を使わない（design §5.6）。boot framebuffer が local に有れば window は開けたまま。
  - 未実施・残り: 実機（p014）。boot framebuffer の解放の呼び出し（最初の flip の後、`ghci_memory_boot_release`）と detach の表示の戻しは p012。KC705 の板が UMA か discrete かは p014 の段 1 で（R24）。design.md の §5.6・§13・§15.3 への反映（export を host にする等）は P3。
