<!-- awesome-plan project=zedbsd record=ws290-p011 -->

# ws290-p011 discrete

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §5.6 の「p011 の実装」と §18 の p011（版 4.14、ghci.md d9252e4）。
- 状態: cleared（2026-10-28 P3 q1644-i01: driver 750・vk 438・全 host 試験 PASS、GHCI 有りの amd64・riscv64 の warning 0、review の M1〜M3・L1〜L7 を反映）
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
  - boot framebuffer の大きさは `FB_STRIDE × FB_SIZE.height`（§13.3 の UMA の式と同じ）、page に広げ local の範囲で切る。DISP2 が無ければ DISP（ID 1）の同じ offset（0x10・0x20・0x28・0x2C）を読む。DISP2 では STATUS[29]（plane 0 の pending flip）の時の FB_NEXT（同じ大きさ）と、DC_CAPS[8] の時の CURSOR_BASE（64 行 × 256 byte = 16 KiB）も外す。重なる・接する範囲は 1 つにする（最大 3 つ）。LOCAL_BASE が page に無ければ local を使わない（log）。
  - VM_FLAG_LOCAL の違反（LOCAL で local の外、LOCAL 無しで local の中）を model は kind 10 にする（N73 の「VM の flag の違反は kind 10」の読み）。

## p012 への引き渡し（前提）

- `ghci_memory_boot_release(device)` は lock を持たずに呼ぶ（中で device.lock、window の数え直しに irq_lock を取る）。呼ぶのは最初の自分の flip が取られた（FB_BASE が新しい番地を示した）後で、かつ boot framebuffer の番地に描く物（framebuffer console、panic の出力）がもう無い時だけ。解放の後はその範囲が直ちに他の確保に渡り得る。
- 解放の後の detach は boot framebuffer に戻さず scan-out を止める（CTRL.ENABLE = 0 と STATUS.idle の待ち）。解放の前の detach は boot の範囲を allocator の外に保ったまま allocator を消し、window は開けたまま（boot console が使い得る）。
- p012 が window を使う時（BLT 無しの表示の写し）は `ghci_memory_window_hold`・`ghci_memory_window_unhold` で挟む（irq_lock 以外の lock を持ってよい）。`allocation->view` は hold の間だけ使う。
- 既知の制限（レビューの L8、変えない）: export した memory が detach より長く残る（別の process が持つ resource）のは host memory と同じ既存の制限。local でも同じ扱い（detach は session が残れば EBUSY）。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
- 2026-10-28 P3 の helper（q1644-i01、仮の扱い）: 実装の途中の記録。
  - code（1）: `memory.c` に local の allocator（LOCAL_BASE からの offset の first fit、`range.c`、1024 run）・`ghci_memory_alloc_local`（0 埋めしない、window が覆えば view）・`ghci_memory_local_start/stop`・boot framebuffer（DISP2 か DISP の CTRL.ENABLE、FB_BASE・FB_STRIDE × 高さ、local の中だけ、allocator から外す）・解放の hook `ghci_memory_boot_release`（p012 が最初の flip の後に呼ぶ）・WINDOW_CTRL（boot framebuffer を保つ間か driver が window を使う間は 1、他は 0）。`ghci_memory_free` は local を allocator に返す。`vm.c` の map は local を `READ | WRITE | LOCAL`（flat は ENOTSUP）。`ghci.c` の start が local を始め、resource の read・write・map は local を window で（覆わなければ ENOTSUP、write は最後の byte を読み戻す、map は DEVICE）。`vk-stream.c` は local の resource を stream にしない。
  - code（2）: `vk-caps.c` の memory の型（discrete は heap 0 local・heap 1 host、型 2 つ）、`vk-memory.c`（型 0 の discrete は local、export・import は host。新しい local は `ghci_vk_execute_clear`（`vk-execute.c`、FILL_BUFFER か BLT の無い時は p010 の copy program）で 0 埋めし ticket を待つ）。
  - 試験: `ghci-model.c`（FILL_BUFFER が session の mapping の先に pattern を書く、VM_MAP の LOCAL の検査、MEM の WINDOW_CTRL の電源投入の値 1）、`ghci-driver-host-test.c`（`scene_local`: boot framebuffer を allocator から外し window を開けたまま・run は framebuffer を避ける・release の後にその範囲を渡し使わない window を閉じる・VM_MAP の flag が `READ | WRITE | LOCAL` で phys が local の番地・host は LOCAL 無し・unmap の後に run が allocator に戻る・window の有る device は driver の用に開けたまま view を持つ・unified は ENOTSUP・host memory の boot framebuffer は扱わない。`scene_local_lost`: LOST の device の local の run は quarantine に入り allocator に残り、detach で返り allocator も消える）、`ghci-vk-host-test.c`（`scene_discrete`: 型 0 が local・VM_MAP の LOCAL・FILL_BUFFER（va・大きさ・0）で 0 埋めし次の byte は触らない・型 1 と export は host で FILL 無し・型 2 は断る・buffer の memoryTypeBits 3・blob の window 経由の upload・readback・map（物理 = BAR2 + offset、DEVICE）・local を超える確保は OUT_OF_DEVICE_MEMORY・free の後に allocator に戻る・window 無しは read・write・map が ENOTSUP で WINDOW_CTRL 0・BLT を落とした discrete は FILL 無しで copy program の DISPATCH。profile の memory の型の期待を discrete で 2 つに）。
- 2026-10-28 P3 の helper（q1644-i01、仮の扱い）: 結果。
  - host 試験: `sh plan/ws290/tests/ghci-host-test.sh` PASS（words 141・ref 56・model 254・driver 641（+54）・old-names・isa・gsi1 50・vk 420（+34）・pipeline 175・execute 198・meta 98、FAIL 0。出力 `build/tmp/ghci290-host.Wmj4C7`）。
  - build（warning 0、exit 0）: `config-amd64-ghci.mk` → `build/ws290-p011-amd64`、`config-visionfive2-ghci.mk` → `build/ws290-p011-vf2`、`config/ci/config-amd64.mk` → `build/ws290-p011-amd64-ci`、`config/ci/config-visionfive2.mk` → `build/ws290-p011-vf2-ci`。最後の comment の直しの後に GHCI の 2 つを差分で build し直し warning 0。`git diff --check` 0。
  - 判断（P3 の確かめを頼む）: (1)（レビューの M3 で改めた、P3「accept」）export の記録（EXPORT_MEMORY_ALLOCATE_INFO）付きの型 0 も local に取る（discrete の EXPORT・WSI の共有の image は local に置く）。host memory に取るのは HOST_VISIBLE の型（型 1）だけ。import は resource の allocation のままで、local の物は importer の session に `VM_FLAG_LOCAL` で map する。(2)（レビューの M2・L2・L5 で改めた、P3「smaller fix」）0 埋めは allocate の中で ticket を待つ。期限は無く、ticket が過ぎるか、reset か device の LOST が ticket を終えるまで待つ（lock は持たない）。待ちの後に device の session が FAULTED・DEAD か device が LOST なら失敗。失敗は全部 OUT_OF_DEVICE_MEMORY（vkAllocateMemory は DEVICE_LOST を返さない。`memory_back` の map の失敗も同じ）。GPU の後の仕事は ring の順なので、client は 0 埋めの前を見ない。(3)（レビューの L1 で改めた、P3「accept」）WINDOW_CTRL.ENABLE は使う物の数（`window_users`、irq_lock）で決める: boot framebuffer を保つ間と、driver が自分の用（boot framebuffer、p012 の BLT 無しの表示の写し）に `ghci_memory_window_hold` している間だけ 1、他は 0（§13.3「if unused」、§11.1「The local window is for the framebuffer only」）。(4)（L1 で改めた）client の local の read・write・map は ENOTSUP（window は client に出さない）。driver の用は `ghci_memory_local_read/write`（window を hold して写し、write は最後の byte を含む word を読み戻してから閉じる）。(5) heap 0 の大きさは LOCAL_SIZE（boot framebuffer の分を引かない）。allocator は 1024 run・1 run 2 GiB まで。表が取れない・boot framebuffer を外せない時は unified に倒す（log）。(6) local の resource は vk の stream にしない（`vk-stream.c`）。(7) flat の discrete は local を使わない（design §5.6）。boot framebuffer が local に有れば window は開けたまま。
  - 未実施・残り: 実機（p014）。boot framebuffer の解放の呼び出し（最初の flip の後、`ghci_memory_boot_release`）と detach の表示の戻しは p012。KC705 の板が UMA か discrete かは p014 の段 1 で（R24）。design.md の §5.6・§13・§15.3 への反映（export を host にする等）は P3。
- 2026-10-28 P3 の helper（q1644-i01、d5740bef3 のレビューの直し、途中）: memory.c（boot の範囲を FB_BASE・DISP2 の STATUS[29] の時の FB_NEXT・CURSOR_BASE の 3 つまで、接する物は 1 つに。LOCAL_BASE が page に無ければ unified。window は使う物の数（irq_lock）で開閉: boot framebuffer を保つ間と driver の hold の間だけ開ける。driver 用の `ghci_memory_window_hold/unhold`・`ghci_memory_local_read/write`（write は最後の word を読み戻す）。`ghci_memory_boot_release` は lock を持たずに呼ぶ。Values referenced を file の先頭から block へ）、ghci.c（client の local の read・write・map は ENOTSUP）、vm.c（出典の形）、recover.c（`lose()` も犯人の書かれた fence を失敗にする、`culprit_fences` に共通化）、vk-memory.c・vk-device.c は下の結果に。driver 試験 750 PASS（+109、`scene_boot_ranges`・`scene_local_fallbacks`・`scene_lost_culprit`、window の hold と driver の accessor）。旧 `lose()` で `lost culprit` が FAIL することを確かめた。
- 2026-10-28 P3 の helper（q1644-i01、d5740bef3 のレビューの直し）: 結果。
  - code: 上の memory.c・ghci.c・vm.c・recover.c に加え、`vk-memory.c`（M2: 0 埋めの待ちに期限無し（1 s ごとに見直し、ticket の通過か reset・LOST の終わりまで）、待ちの後に device の session が FAULTED・DEAD か device が LOST なら失敗。M3: export の型 0 も local。L5: 確保の失敗は全部 OUT_OF_DEVICE_MEMORY（`memory_back` の map の失敗も））、`vk-device.c`（L4: deviceType は `ghci_memory_local_bytes() != 0` の時だけ DISCRETE_GPU）、`ghci.h`（boot の範囲の配列 `boot_count`・`boot_offset[3]`・`boot_bytes[3]`、`window_users`、新しい関数の宣言）。
  - 試験: driver 750（+109: `scene_local` に window の hold・unhold・driver の accessor・window 無しの拒否・2 度目の release、`scene_boot_ranges`（DISP2 の FB_NEXT・cursor と接する範囲の結合・pending flip 無し・DISP（ID 1）・止まった DISP2・local の始まりと終わりを跨ぐ・page の中から始まる・release の前の detach で window が開いたまま）、`scene_local_fallbacks`（LOCAL_BASE が page に無い・walker 無し）、`scene_lost_culprit`（旧 `lose()` では FAIL）)。vk 438（+18: `scene_discrete` を export の local・importer の `VM_FLAG_LOCAL`・client の read・write・map の ENOTSUP・driver の accessor に、`scene_discrete_clear`（1 分の時計の間 GPU が止まっても待ち続ける、待ちの中の device の fault と reset で OUT_OF_DEVICE_MEMORY（session は DEAD、run は reset の後に返る）、通った 0 埋めでも session が犯人なら OUT_OF_DEVICE_MEMORY で run は unmap の ticket の後にだけ返る））。
  - host 試験: `sh plan/ws290/tests/ghci-host-test.sh` PASS（words 141・ref 56・model 254・driver 750・old-names・isa・gsi1 50・vk 438・pipeline 175・execute 198・meta 98、FAIL 0。出力 `build/tmp/ghci290-host.zNaZMX`）。model は変えていない。
  - build（warning 0、exit 0）: `config-amd64-ghci.mk` → `build/ws290-p011-amd64`（vmunix check PASS）、`config-visionfive2-ghci.mk` → `build/ws290-p011-vf2`。`git diff --check` 0。CI の 2 つの config は今回は流していない。
  - 未実施・残り: 実機（p014）。design.md の §5.6（版 4.14 の段落: export を local に、0 埋めの待ちに期限無し・失敗は OUT_OF_DEVICE_MEMORY、window は使う物の数で開閉、client の local の read・write・map は ENOTSUP、boot の範囲 3 つ、LOCAL_BASE の page の検査）と §8（window の数は irq_lock）への反映は P3。M2 の「reset 中の待ちで run は unmap の ticket の後」は、device の fault の reset では session が DEAD になり unmap は packet 無しで落とす（reset が GPU の到達を終えている）ので、unmap の ticket の後に返る形は犯人の場面で試した。

- 2026-10-28 P3（q1644-i01）: review（Opus 5.5、d5740bef3）H 0・M 3・L 8、判定「条件付きで承認可」。helper が M1（boot の解放は lock 無しで呼ぶ、p012 の前提を記録）・M2（0 埋めの待ちに期限無し）・M3（export も local、P3 の判断）・L1（client の local の CPU の触りは ENOTSUP、window は driver の数で開け閉め）・L2（`lose()` も犯人の書かれた fence を FAILED に）・L3〜L7 を直し試験を足した。L8 は host と同じ既存の制限として記録。M2 の「reset の間の待ち → unmap の ticket の後に run が返る」は、reset で session が DEAD になり unmap が packet 無しになる（reset が GPU の触りを止めている）ので、ticket の後に返る事は犯人の場面で確かめた（P3 が了承）。design 版 4.14 の §5.6 の段落を直した。確かめた事: `plan/ws290/tests/ghci-host-test.sh` PASS（driver 750・vk 438 ほか）。helper が GHCI 有りの amd64・VF2 を build し直し warning 0。GHCI 無しの CI の 2 つは p011 の実装の時に warning 0（GHCI の file を含まない）。
