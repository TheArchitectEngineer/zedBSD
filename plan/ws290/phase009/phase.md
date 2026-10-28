<!-- awesome-plan project=zedbsd record=ws290-p009 -->

# ws290-p009 記録と実行

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p009（版 4.5、ghci.md cb937e6）。
- 状態: in-progress
- 依存: p008

## 範囲

記録と実行: vk-pass・vk-record・vk-execute、draw・indirect・dispatch・barrier・dynamic state・転送・query・event・clear

詳細は design の該当の節（§4〜§16）と §14 の p009 の場面。

## 受け入れ

- p009 の場面 PASS、amd64・riscv64 の build の warning 0
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 36 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19: N78（属性の register の累積の base_i と header の 4 × loc + comp の食い違い）は ghci.md の累積で作る。p006 の compiler の取り決め（design §11.4 の「p006 で決めた取り決め（版 4.10）」: STORAGE の image は linear・set 3 の配置・texel buffer は slot と entry・属性の表は常に渡す・VS を先に compile・dispatch と draw の間の table の置き直し・c24〜c31 に LOAD_CONST しない・fragment の無い pipeline の FS）を守る。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
- 2026-10-28 P3（q1644-i01）: p008 の review から p009 に渡す物: submit が参照する pipeline・set・view・buffer を fence まで hold する、code の heap（EXEC）と data の heap（EXEC 無し）を分ける（design 版 4.12 §11.4）、dynamic offset は pipeline layout の set ごとの base から作る、`subpass_of` の不正な render pass は断る、`ghci_vk_layout_tables` の ENOENT は draw を飛ばして log。
- 2026-10-28 P3 の手伝い（q1644-i01、途中の記録）: code を入れ、既存の host 試験が通る所まで。commit は P3。
  - 新: `src/drivers/gpu/ghci/vk/vk-pass.c`（render pass・framebuffer・granularity）、`vk/vk-execute.c`（記録 → `CMD_*`: bind・draw・indirect・dispatch・barrier・dynamic state・render pass・転送・query・event・clear・execute commands）。変更: `vk.h`（heap の code・data の分離、retire の list、render pass・framebuffer・query pool・submission の型）、`vk-memory.c`（heap 2 つ、`ghci_vk_heap_retire`・`ghci_vk_heap_reclaim`）、`vk-queue.c`（submit が `ghci_vk_execute` の packet を置く、query pool の create・destroy・results）、`vk-object.c`・`vk-dispatch.c`・`vk-pipeline.c`（`subpass_of` は session に無い render pass と範囲外の subpass を断る、run は retire）、両 platform の vmunix.mk の一覧、`ghci-host-test.sh` の executor の一覧、`ghci-vk-host-test.c`（記録の有る submit の期待を「bind の無い draw で EINVAL」に、event の語は data heap）、`ghci-pipeline-host-test.c`（`make_render_pass`、pipeline は render pass 0x7777 を作ってから）。
  - `sh plan/ws290/tests/ghci-host-test.sh` rc=0（vk 383・pipeline 175 ほか、p009 の場面はまだ無い）。kernel の build はまだ。
  - build（timeout 3000、ここまでの code）: `config-amd64-ghci.mk`（build/ws290-p009-amd64）・`config-visionfive2-ghci.mk`（build/ws290-p009-vf2）とも rc=0・warning 0、vk-pass.o・vk-execute.o が両方に入った。次: p009 の場面の試験（`ghci-execute-host-test.c`）。
  - 試験: 新しい `plan/ws290/tests/ghci-execute-host-test.c`（`ghci-pipeline-host-test.c` の末尾で include し、同じ binary に `execute` を渡して流す。`ghci-host-test.sh` の pipeline の節の後に足した。pipeline の試験に `test_caps_off`・`test_ring_bytes` と `make_render_pass` を足した）。場面: frame（render pass の clear・pipeline と R07 の状態・vertex buffer・draw・dispatch の set 2・LOAD_CONST c0〜c23 と c32〜c40・頭と末の barrier を手で書いた列と比べる、description の byte と data heap が EXEC でない事、dispatch と draw の間の table の置き直し）、binds（pipeline・dynamic viewport・vertex buffer・render pass・set の無い draw/dispatch は EINVAL で何も置かない）、indirect（有る時の DRAW_INDIRECT・INDEXED・DISPATCH_INDIRECT、CAP_INDIRECT_DRAW 無しで飛ばす）、barriers、transfers（COPY_BUFFER・R11 の中央・FILL・UPDATE・buffer↔image・COPY_IMAGE・BLIT、no-blt で ENOTSUP）、queries（reset の FILL、timestamp と availability の FENCE、occlusion、host の結果、GPU の SetEvent・WaitEvents の WAIT_FENCE・ResetEvent、誰も set しない event の wait は ENOTSUP、host の set の後は通る、statistics の pool は FEATURE_NOT_PRESENT）、clears（ClearColorImage の clear だけの pass、ClearAttachments の全体と部分）、large（4 KiB の ring に 400 draw、vertex の size_bytes 192）、fan、holds（fence 前の destroy で run が残り、後で戻る）。拒否・飛ばしの理由は quiet を外して log で確かめた。
  - `sh plan/ws290/tests/ghci-host-test.sh` rc=0: words 141/0、ref 56/0、model 254/0、driver 587/0、gsi1 50/0、vk 383/0、pipeline 175/0、**execute 115/0**。
  - 追加（2026-10-28 13:17）: 場面 subpasses（4 sample の 2 subpass: subpass 0 の colour 0・1・depth の clear と store・DONT_CARE、colour 0 の resolve は description、colour 1 は subpass の後に RESOLVE_IMAGE、subpass 1 は LOAD・STORE、stencil の無い D32 は CLEAR_STENCIL を立てない）と secondary（ExecuteCommands で secondary の draw が primary の pass の中で走る、secondary だけの submit は EINVAL）。`vk-execute.c`: blit の offset の配列は wire で [u64 2] が前に付く（直した）、stencil を持たない format では CLEAR_STENCIL を立てない。
  - 結果: `sh plan/ws290/tests/ghci-host-test.sh` rc=0: words 141/0、ref 56/0、model 254/0、driver 587/0、old-names PASS（38 sources）、ghci-isa PASS、gsi1 50/0、vk 383/0、pipeline 175/0、**execute 131/0** → PASS ghci290-host-test。build（timeout 3000、最後の source の変更の後）: `config-amd64-ghci.mk`（build/ws290-p009-amd64）・`config-visionfive2-ghci.mk`（build/ws290-p009-vf2）・`config/ci/config-amd64.mk`（build/ws290-p009-amd64-ci）・`config/ci/config-visionfive2.mk`（build/ws290-p009-vf2-ci）とも rc=0・warning 0。`git diff --check` 空。QEMU・実機は無し。
  - **決めた事（P3 の判断を頼む）**:
    - heap の寿命: submit が pipeline・set・view・buffer の record を参照で持つ代わりに、packet を作る時に descriptor の byte を submit の一時の memory に写し、GPU が読む heap の run（pipeline の PSO・code・set 3、scratch、event の語、query pool）は destroy の時に device の最後の ticket に結んで（`ghci_vk_heap_retire`）、その fence の後に返す（`ghci_vk_heap_reclaim`、heap_take と device の終わりで）。buffer・image の memory は VM_UNMAP の fence の順で守られる。submit の一時の memory はその submit の ticket で返す。置き損じ（EIO、一部が ring に入った）の一時の memory は device の heap と一緒に返す。
    - code の heap（EXEC: PSO・code・set 3）と data の heap（EXEC 無し: scratch・event・query pool・一時の memory）を分けた。
    - bind の検査は submit の変換の時（記録は wire のまま持つので結果は同じ）。pipeline・使う set・要る dynamic state・vertex buffer・index buffer が無い draw/dispatch と、render pass の外の draw は submit を EINVAL で断る（frame が止まり何も置かない）。`ghci_vk_layout_tables` の ENOENT はその draw を飛ばして log。
    - dynamic state: pipeline が変わった draw の前に BIND_PIPELINE、viewport・scissor（rasterizerDiscard は 0×0）、depth bias・blend constants・stencil（R07、毎回）、dynamic の line width、MRT_BLEND の attachment ごとの語。同じ pipeline では記録が変えた dynamic の値だけ。
    - table: sets 0〜2 は中身の有る物だけ BIND_DESCRIPTOR_TABLE（count = 個数、set 2 は 16 byte 単位）、set 3 は `table_va` が有る時（count 2）。LOAD_CONST は c0〜c(push の語数, 24 まで) と、push が 96 byte を超えれば c32 から、そうでなければ c40 から bank の終わりまで（c24〜c31 は書かない）。graphics と compute の table は持ち主が変われば置き直す。
    - barrier の写し: stage は VF・VS・FS・ROP・CS・BLT に（BOTTOM・ALL_COMMANDS は全部、HOST は 0）。src の書き込みの access → FLUSH_L1_DATA、dst の読み → INVAL_L1_DATA（shader・input attachment・memory の読みは INVAL_TEX も）、host の書き込み → INVAL_L1・TEX・DESCRIPTOR、host の読み → WAIT_IDLE・FLUSH_L1・FLUSH_L2。
    - render pass: attachment は view の番地と行（linear）、mip・layer 0、layers 1。load は最初に使う subpass でだけ attachment の op、後は LOAD。store は最後に使う subpass でだけ attachment の op、前は STORE。clear は `clear_color_table_va` の表（colour の 16 byte）と packet の depth・stencil の値。integer の format の resolve は sample 0（mode 3）、他は average。framebuffer の layers > 1 は layer 0 だけ描き log（layer 1 以降の clear は未実装）。GET_RENDER_AREA_GRANULARITY は {1, 1}。render pass の attachment 32・subpass 16 を超える物と framebuffer の 16384・2048 を超える物は OUT_OF_HOST_MEMORY、device の持たない sample 数・MAX_MRT を超える色は FEATURE_NOT_PRESENT。`subpass_of` は session に無い render pass と範囲外の subpass で pipeline を断る（p008 の試験は pipeline の前に render pass を作るようにした）。
    - 転送（CAP_BLT 有り）: CopyBuffer は R11 の中央を COPY_BUFFER、端の byte と word の中の位置が違う copy は飛ばして log（§11.6 の compute は p010）。buffer↔image は slice ごと、row pitch は rowLength（圧縮は block の行）。D24S8・D32S8 の片方の aspect は飛ばす。3D の blit は飛ばす。CAP_BLT 無しの転送（ResetQueryPool の fill を含む）は submit を ENOTSUP で断る。CopyQueryPoolResults は飛ばして log（p010）。
    - query pool: occlusion と timestamp だけ（statistics は FEATURE_NOT_PRESENT）、65536 個まで、data heap の 16 byte の slot、作成で 0。結果は host が slot を読み、全部 available なら SUCCESS、でなければ NOT_READY（libvulkan が待つ）。WriteTimestamp の stage: TOP_OF_PIPE → 0、early fragment test・fragment shader → 1、他 → 2。
    - event（R10 の一部）: SetEvent・ResetEvent は event の語への FENCE（MEM_WRITE、値 1・0）。WaitEvents は、同じ submit で前に GPU が set した event は WAIT_FENCE、host が set して GPU の書き込みが残っていない event は待たない。それ以外（R10 の (b)、kernel が後の submit を保留する形）は未実装で submit を ENOTSUP で断る。
  - **未実施・残り**: R10 の (b) の保留。framebuffer の layer 1 以降の clear（Q12）。ClearAttachments の部分の rect（R31）と端の byte の copy・CopyQueryPoolResults・no-blt の転送（p010）。depth・stencil の image の clear（code は有るが試験していない）、BLIT の 3D、圧縮 format の copy、input attachment を読む pipeline の draw は試験していない。新しい code の規約の全文の見直し（条件の中の関数の呼び出しなど）は p015。消してよい file は無い。
