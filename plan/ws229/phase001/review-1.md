# ws229-p001 review-1（設計の敵対的 review、2026-10-14、Q1 が起動した design-reviewer）

## ws229-p001 設計（Gen9・Gen9.5 対応）の敵対的レビューの結果

対象は /home/awe/zedBSD-claude1/plan/ws229/phase001/phase.md です。main（d1e743394）の `src/drivers/gpu/i915/` と照らしました。一次資料は /home/awe/zedBSD-worktrees/p1/build/ws229-p001/linux/linux-6.8.12 と /home/awe/zedBSD-worktrees/p1/build/mesa-tools/mesa-25.0.7 を読み取りだけで使いました。source、plan、build には何も変更を加えていません（build・QEMU・実機の試験も実施していません）。実行したのは grep・sed・cat・wc による読み取りだけです。

以下で phase.md の行番号は「phase.md:NN」と書きます。

---

## 致命的（このまま実装すると Gen9 で最初の request で止まる、または受け入れ試験で検出できない）

### F1. `request.c` がどの Phase の担当にもなく、中身はすべて Gen12 専用
- **根拠**:
  - `request.c:129-146` が AUX table の invalidate を出す。`MI_LRI_MMIO_REMAP_EN` 付きの LRI と、`MI_SEMAPHORE_WAIT_TOKEN | MI_SEMAPHORE_REGISTER_POLL` の組。
  - `context.c:280-300` の `drv_i915_lrc_aux_inv_reg()` は engine の ID だけで register を選ぶ。このため Gen9 の RCS0 にも `GEN12_CCS_AUX_INV` が返る。
  - `request.c:396-402` は MI_ARB_CHECK の bit 8 を使う（preparser。Gen12 専用）。
  - `request.c:317-330` と `:479-503` は PIPE_CONTROL に Gen12 専用の bit を立てる。dw0 の bit 9（HDC）、dw1 の TILE_CACHE（bit 28）、FLUSH_L3、COMMAND_CACHE（bit 29）。gen90.xml の PIPE_CONTROL にはこれらの bit がない。
  - `request.c:632` は `MI_SEMAPHORE_WAIT_TOKEN` を使う。これは `intel/commands.h:95` で 5 dword の GEN12+ 形式。Gen9 は 4 dword の形式で、Linux では `gen8_engine_cs.c:629-641` の `emit_preempt_busywait`。
  - 対照として、Linux の Gen9 は `gen8_emit_flush_rcs` を使う（`gen8_engine_cs.c:13-77`）。ここには Gen9 の VF invalidate の前に null の PIPE_CONTROL を出す処理（:42）と、KBL < C0 の WaForGAMHang（:46）がある。
- **起こる状況**: Gen9 には register poll の mode がない。このため semaphore は GGTT の番地 0x4208 付近、つまり GOP の framebuffer の領域を polling することになる（推測。PRM の bit の割当てから導いた）。LRI の書き先は Gen9 では別の register になる。どちらでも最初の flush を含む request で hang する見込みが高い。
- **phase.md の欠け**: §2 の表（phase.md:58-78）に「request の flush・breadcrumb・preempt busywait」の行がない。§5 の担当の一覧（phase.md:208）にも `request.c` がない。
- **直し方**: §2 に request.c の行を足し、p002 の担当にする。中身は次の 4 点。
  - gen8 形式の emit_flush_rcs/xcs（Gen9 の VF の null PC、KBL < C0 の DC flush の WA を含む）
  - gen8 形式の fini breadcrumb（rcs: RT/DEPTH/DC flush の後に、FLUSH_ENABLE と CS_STALL 付きの ggtt write）
  - 4 dword の MI_SEMAPHORE_WAIT
  - Gen9 では AUX invalidate と preparser を出さない（`lrc_aux_inv_reg` も版で引く）
- **効く Phase**: p002（ほかに p005）。

### F2. 2 payload の SEND は 2 か所ではなく約 18 か所にある。並べ直す案は見積もりも受け入れも成り立たない
- **根拠**: `COMPILE_EX_MLEN` を使う呼び出しは次のとおり。
  - `compile.c:1772, 2491, 2502, 3001, 4871, 4950, 4964, 5004, 5019`
  - `compile-compute.inc:334, 345, 485, 620, 631`（A64・untyped の store、atomic）
  - `compile-geometry.inc:467, 548, 741, 751`（VS・GS の URB write。EOT を含む）
  - `compile-geometry.inc:461-468` は header を r(MAX)、data を r(MAX-1) に置く。つまり data が header より前の register にある。
- **phase.md の誤り**: phase.md:173 は「今の RT write・scratch の spill」だけと書く。phase.md:254 は「register の圧迫は小さい（6 register）」と書き、SENDS を退けている（phase.md:173, 244）。
- **起こる状況**: 直し漏れた箇所は、mlen が data を含まない Gen9 の SEND になる。これは「有効な命令」なので、disasm→asm の往復（phase.md:209 の受け入れ）は通る。壊れるのは意味だけで、実機の p005 まで見つからない。対象は VS の出力、compute の書き込み、atomic のすべて。
- **直し方**: Gen9 では SENDS・SENDSC（opcode 51・52）を出す。Mesa も `brw_eu_emit.c:1472-1531` で同じことをしている。field は次のとおり。
  - src1 の reg: 51:44
  - ex_desc[31:16]: 95:80
  - ex_mlen: 67:64
  - src1 の file: 36
  - こうすれば generator は変えずに済む。§3.4 の「1 payload に並べ直す」の行と §7 の SENDS を退けた理由は撤回する。
- **効く Phase**: p003。見積もりの 2.0 LW と §0(a) の「9 割以上そのまま」も見直しが要る。

---

## 重大

### M1. Gen9 の display・PCH の割込みの reset が設計にない（MSI の storm と master の衝突）
- **根拠**:
  - `device.c:2208-2217` の interim の reset は `I915_DISPLAY_INT_CTL` 0x44200 に 0 を書くだけ（`device.c:68`）。0x44200 は Gen9 では `GEN8_MASTER_IRQ`（Linux `i915_reg.h:4279`）にあたる。
  - Gen8・9 の master の DE・PCH・PCU の bit は状態の表示で、有効化の bit ではない。Linux は `gen8_irq_reset`（`i915_irq.c:710-723`）で次を reset している。
    - EDP_PSR の IMR・IIR
    - 電源の入っている pipe の DE_PIPE（`intel_display_irq.c:1415-1433`）
    - DE_PORT、DE_MISC、GEN8_PCU
    - SDE（`ibx_irq_reset` 662-673）
- **phase.md の誤り**: phase.md:72 と :193 は「mask のまま」「master の display の bit を enable しない」と書いている。この前提は Gen9 では成り立たない。
- **起こる状況**: firmware が IER を有効にしたまま残していると、無視する handler が master を戻すたびに MSI が再び発火する。SDE の hotplug（monitor の抜き差し）でも同じことが起こる。
- **直し方**: Gen9 用の interim の ops を作る。
  - PW の状態を見たうえで DE_PIPE（A は PW1、B・C は PW2）の IMR・IER・IIR を reset する。
  - DE_PORT 0x44440、DE_MISC 0x44460、PCU 0x444e0、SDE 0xc4000 系の IER を 0、IIR を clear にする。
  - 0x44200 には書かない。
- **効く Phase**: p002。

### M2. 担当の表（§5）の漏れと重なり
- **担当のない file**（Gen9 で変更が要るもの）:
  - `request.c`（F1）
  - `display/display.c`
    - native check の配線（:1799-1876）
    - `noirq_front` の power_domains_init と vga_register は absent より前に走る（:1777-1781）
    - `drv_i915_display_irq_prepare`（:475-502）は absent が立つ前に PCH を判定し、Gen11 の display irq ops を bind する。§4.2 の「display の世界を作った直後に absent」は device.c で実装できるが、`n0_pipe_powered` の配線（:1824）と gop_output_read は display.c にある。
  - `verify-workarounds.c:54-90, 129, 156, 219`（Gen12 の MCR の範囲で multicast を判定している。Gen9 は Linux の `mcr_ranges_gen8`）
  - `defaults.c`（M4）
  - `worker.c`（:357-363 で VCS0 を video に選ぶ）
  - `render/batch.c:100-145`（PIPE_CONTROL の helper が RT flush に HDC flush を自動で足す）
  - `render/heap.h`（:148-175 の scratch の ID、:194 の `I915_MOCS_UNCACHED_INDEX 3`）
  - `render/fence.c`、`render/vulkan.c:104-109`（video の gate）、`render/instance.c:284`（"Gen12 Xe"）
  - `tests/fixtures/draw-fixture.c:428, 1081`
  - `include/kern/boot.h`（phase.md:194 の新しい boot の key）
- **重なり**: p003 は `plan/ws101/tests/host/`（directory 全体）を持ち、p004 は `plan/ws101/tests/host/genxml-check.py` を持つ。同じ path が両方に入っている。
- **規則との衝突**: p003・p004 が他の WS の plan/tests（ws101・ws075・ws031・ws083）を直す設計になっている。AGENTS.md の「subagent の修正可能範囲」は、割り当てられた WS の `plan/wsNNN/` だけを認めている。Q1 の許可を取るか、`plan/ws229/tests/` に写して使う必要がある。
- **WS229 の外との衝突**: WS051・WS113・WS083・WS075 と bug の修正が、同じ時期に device.c・display/*・render/* を変えている。§5 は「3 人の衝突」しか見ていない。
- **効く Phase**: p002・p003・p004。§5 の表を直すのは Q1。

### M3. RPCS（R_PWR_CLK_STATE）が Gen11 の layout のまま
- **根拠**:
  - `context.c:245-248` と `:495-523` は slice の数を `GEN11_RPCS_S_CNT_SHIFT` の位置に置く。
  - Gen9 の slice の数は GEN8 の位置（S_CNT 17:15）にある。Gen9 は `has_eu_pg` が真なので、EU の min・max（3:0・7:4）も要る（Linux `intel_sseu.c` の make_rpcs）。
  - §2 の context の行（phase.md:65）に RPCS がない。
- **起こる状況**: GT3・GT4（`has_slice_pg`）では Gen9 の S_CNT が 0 になり、slice や EU が gate されたまま thread が dispatch されない可能性がある（推測）。GT2 では RPCS が 0 になり、EU の PG の既定に任せることになる（未確認）。
- **直し方**: Gen9 の make_rpcs を移す。
- **効く Phase**: p002。

### M4. Gen9 の golden render state（`gen9_null_state`）が抜けている
- **根拠**:
  - Linux は default の記録の時に `intel_renderstate.c:21-31` で Gen6〜9 に null state の batch を流す。Gen9 では `gt/gen9_renderstate.c`（MIT、981 行）。Gen12 は流さない。
  - zedBSD の `defaults.c` はどの Phase の担当にもない。
- **直し方**: p002 の担当に defaults.c を足し、値を転記する（出典を注記）。engine の MOCS は Gen9 では RCS の context に保存される。このため MOCS の書き込みは record_defaults より前、かつ reset の後にも行う。
- **効く Phase**: p002。hang の危険は推測で、確かめるのは p005。

### M5. fence の意味（Gen12 の encoder の直列化を前提にしている）
- **根拠**:
  - `compile-compute.inc:644-650` は「encoder が各 reply を待つので acquire にはそれ以上要らない」と書いている。
  - `eu.c:18-40` は SEND のたびに token を置き、sync.nop で完了を待たせている。
  - phase.md:156 と :162 は Gen9 では「sync は no-op」「不要」としている。
  - Mesa は ver < 12 では fence の reply を `mov(1) null:UW reply:UW` で読み、stall させている（`brw_generator.cpp:1215-1240`）。
- **起こる状況**: Gen9 の hardware の scoreboard は、読まれない reply register では止まらない。fence が効かず、memory の順序が保証されない。間欠的に起こり、実機の試験でも見つけにくい。
- **phase.md の誤り**: §3.3 の「mixed float は使わない」も誤り。`compile.c:3612-3641` の half の pack・unpack（F と HF の MOV）は SKL PRM の mixed mode にあたる。
- **直し方**: §3.4 に 7 点目として、Gen9 では fence の reply を読む MOV を出すことを足す。
- **効く Phase**: p003。

### M6. 受け入れ試験が書かれたとおりには走らず、誤りも見つけにくい
- **ktest-gt は host では走らない**:
  - ktest-gt は kernel の中の試験で、device の起動の後に動く（`tests/execution/README.md`「How a test runs」）。QEMU には Intel の GT がないので走らない。
  - phase.md:208 の p002 の受け入れ（ktest-gt の register model で PASS）と phase.md:219（「全部 QEMU 無し」）は矛盾している。実際には 5330 の実機と T1 が要る。
- **往復の試験は「意図」を見ない**:
  - `plan/ws031/tests/run-vk-gentool-test.sh` の judge は、byte ではなく disasm の text の一致を比べている（:36-44）。phase.md:209 の「byte が往復で一致」とは違う。
  - opcode の値の取り違え（AND を OR の値で出すなど）や SFID・descriptor の誤りは、往復しても text が揃うので通ってしまう。
- **Gen9 の SEND の ex_desc**: src0 の region の bit 88:85・83:80・67:64 と src1 の type 94:91 は、Gen9 の SEND では ex_desc[31:16] にあたる（`brw_eu_inst.h:816-822`）。disasm は plain の SEND の ex_desc を表示しない（`brw_disasm.c:2149-2160`）。§3.2 の表にもこの行がない。
- **直し方**:
  - Gen9 用の期待の text を brw_asm で byte に組み立て、zedBSD の byte と比べる試験を加える（i915-vk-gentool-eu.c の Gen9 版）。
  - Gen12 は refactor の前後で byte が同一であることを確かめる。
  - p004 は Gen12 の batch の byte が前後で同一であることを確かめる。
  - p002 は mock の mmio で Gen12 の起動の書き込み列の golden を取るか、5330 の T1 を必須にする。
- **効く Phase**: p002・p003・p004。

### M7. suspend・resume と D3 で GOP の画面が消える（Gen9 では display absent が通常の状態になる）
- **根拠**:
  - `display.c:840-843` は `power_domains_inited` を見るだけで、absent は見ない。
  - `i915.c:381-385` は suspend が成功すると「D3hot へ」と書いている。
  - Gen9 では absent が正常の状態なので、蓋を閉じる・S0ix・suspend のたびに iGPU が D3hot になる。GOP の pipe は戻らないので、画面が黒いまま戻らない。
  - RC6 を off にしていると GT は RC0 のままなので、package の C-state や S0ix にも入れない。
  - 設計（§4・§7）は suspend の扱いに触れていない。
- **直し方**: Gen9 で display が absent の間は suspend を拒否するか、D0 を保つ。resume の経路（PPAT・MOCS・WA・gtt WA・ELSP の再設定）も版で分ける。
- **効く Phase**: p002・p005。**ユーザーの判断が要ります**（U2）。

### M8. Gen9 の PCI ID を既定で match させると、今の Gen9 機の起動を壊すおそれ（推測）
- **根拠**:
  - `i915.c:66-70` の表に Gen9 を足すと、i915 が attach し、display のない GPU node を公開する。
  - compositor は `scanout_display()`（`libkeiland-backend-zedbsd/scanout-zedbsd.c:270-311`）で display がないと ENOTSUP を返す。今の firmware framebuffer と llvmpipe の経路に戻るかは確かめていません。
  - GT の起動に失敗した場合の後始末も、Gen9 の実機では一度も通っていない。
  - 加えて `render/vulkan.c:104-109` は、video を boot の指定と VCS0 の有無だけで有効にする。`uabi_capabilities` は見ていない。そのため Gen9 で video の boot の指定があると、Gen12 の MFX を流してしまう。
- **直し方**: p005 が PASS するまで、Gen9 の match を boot の指定（例 `i915.gen9=on`）でだけ有効にする。video は版で gate する。
- **効く Phase**: p002（p004）。**ユーザーの判断が要ります**（U1）。

### M9. p004 に Gen9 の 3D・compute の必須の workaround が抜けている（Mesa の anv の `GFX_VER == 9` の分岐）
| 内容 | 根拠（`src/intel/vulkan/`） |
| --- | --- |
| VF cache は address の下位 32 bit しか見ない。4 GiB をまたいで同じ下位 bit を持つ VB・IB は古い値を読む | `genX_cmd_buffer.c:4847-4860` |
| VF invalidate の前に null の PIPE_CONTROL が要り、post-sync の書き込みも必須 | `genX_cmd_buffer.c:1816-1820, 2521-2522` |
| ver < 12 では HDC の flush を DC flush（dw1 bit 5）に置き換える | `genX_cmd_buffer.c:2506-2509` |
| PIPELINE_SELECT(GPGPU) の前に `3DSTATE_CC_STATE_POINTERS` を消す | `genX_cmd_buffer.c:4721-4733` |
| GPGPU から 3D に戻る時に MEDIA_VFE_STATE を出し直す | `genX_cmd_buffer.c:4754-4780` |
| 3D から GPGPU に移る時に MEDIA_CURBE_LOAD を出し直す | `genX_cmd_buffer.c:4771-4785` |
| GPGPU の post-sync には CS stall が要る | `genX_cmd_buffer.c:1746-1748` |
| CACHE_MODE_1 の初期化 | `genX_init_state.c:398-406` |

- zedBSD 側の該当箇所:
  - HDC の flush は `render/batch.c:115, 142` にあり、担当がない（M2）。
  - `render/compute.c:356` の max_threads は `GEN12_VFE_THREADS_PER_DSS`（112）× DSS で、Gen9 では 1 subslice あたり 56。
  - p004 の表（phase.md:210）の「HDC flush を出さない」は、HDC flush を DC flush に置き換える形に直す。
- **効く Phase**: p004。

### M10. N0-lite の電源の対応の誤りと、VT-d の結果を捨てている点
- **電源の対応の誤り**:
  - Linux の SKL では TRANSCODER_A は PW2 にある（`intel_display_power_map.c:306-326`）。
  - phase.md:192 の「PG1 bit 28 → pipe A と eDP の transcoder」は不正確で、TRANSCONF A（0x70008）を読むには PW2 が要る。
  - laptop の GOP は普通、transcoder EDP（0x7f008、PW1）で出力する。
  - bit の位置（PW1 の STATE は 28、PW2 の STATE は 30）は `HSW_PWR_WELL_CTL_STATE(idx)` で正しいと確認しました。
- **VT-d の結果を捨てている**:
  - phase.md:192 は「結果に関わらず absent」とする。この場合、VT-d の translation や PMR（`takeover.c:2767-2780`）が有効でも GT を起こし、GT の DMA が fault する。
  - この危険は今の Gen12 の経路にもあります。GT 側で VT-d を見ている所はありません（grep で確認）。
  - overlap が見つかった時の扱いも決まっていない。
- **直し方**: N0-lite で VT-d の translation や PMR を見つけたら、GT の起動を止める（EIO と log）。読む register は eDP の transcoder と pipe A の plane（PW1）にする。
- **効く Phase**: p002。

---

## 中

1. **Gen12 の経路を壊す危険**（p002）
   - `device.c:1017-1023` は、TGL 以外の product を既定で ADL-P（display_ver 13）と扱う。
   - `workarounds.c:223-279` は `graphics_ver >= 4` で Gen12 の engine の WA を当てる。その中に Gen9 にない RING_CMD_CCTL がある。
   - `workarounds.c:629` は `graphics_ver < 12` なら「MOCS なし」で return する。
   - `graphics_ver` が既定の 0 のまま通ると、WA が空になったまま verify も通り、Gen12 が黙って壊れる。
   - 直し方: product → {graphics_ver, display_ver, platform} の明示の表にする。表にない product と 9・12 以外の版は起動を拒む。`graphics_ver` の比較は全部見直す。
2. **clock**（p002）: phase.md:63 の「24 MHz 固定」は誤り。Linux は `gen9_read_clock_frequency`（`intel_gt_clock_utils.c:89-109`）で CTC の shift（`freq >>= 3 - shift`）と divide logic の経路を見る。SKL は普通 12 MHz なので、timeout や timestamp が 2 倍ずれる。
3. **PXP**（p002）: `device.c:1808` は `has_pxp=1` の定数で `pxp_init` を呼ぶ（KCR は Gen12 のもの）。Gen9 では 0 を渡す。
4. **RT write の header**（p003）: header を使う時（MRT・dual source）に discard があれば、`header.15` に sample mask を入れる必要がある（`brw_lower_logical_sends.cpp:363-366`）。§3.4 の RT write の行に足す。
5. **Phase の依存の漏れ**（Q1）:
   - p004 は p003 の `drv_i915_shader_compile_gen()`（pipeline の呼び手は p004 が持つ）と、p002 の SSEU（subslice・EU の数）に依存する。WS の表は p004 の依存を「p001・p002 の契約」だけにしている。
   - §5 の冒頭（phase.md:204、p002 の最初の commit で compile の引数を足す）と phase.md:215（p003 が `_gen` を新設し、古い名前を wrapper にする）が矛盾している。
6. **RC6 off**（p002）: 「有効にしない」だけでは足りない。BIOS が残した RC_CONTROL・GEN9_PG_ENABLE に明示で 0 を書く。PCODE の ring・IA の freq の表がない（性能だけの影響）。S0ix に入れないことを制限として記録する。
7. **事前に組んだ Gen12 の EU の binary**（p005）: `tests/fixtures/vkref-generated.inc` と `tests/execution/draw-test.c` の draw・tex 系の scenario は、Gen9 では拒否する。
8. **MOCS の index**（p004）: `render/heap.h:194` の index 3 は skl の表では未使用で、index 1（PTE）として扱われる。isl の Gen9（UC 0、internal 2<<1）に合わせる。

## 軽微

- phase.md:236 の PCI ID の 86 個について。header の範囲（331-542）には BXT・GLK（Gen9 LP、LLC がなく VT-d の GGTT の WA も違う）が含まれ、全部で 93 個ある。除外することを明記する。
- `memory.h:47-50` は render context を約 30 page と見積もっている。Gen9 では render の image が 22 page あるので約 38 page になる（窓には収まる）。
- `render/instance.c:284` の device 名が "Gen12 Xe" のまま。
- Linux の tarball の GPG の検証は未実施（phase.md:48 にも記録あり）。
- 原文の「GOPのバッファのままで表示できるようにして」と D3（display のない node では Keiland を動かさない）の整合。決定は記録されていますが、Beta 2 の説明で取り違えが起きないよう、Q1 が改めて確認するとよいと思います。

---

## §3.2 の field の表で確かめたこと（正しかったもの）

Mesa の `brw_eu_inst.h:319-436, 899-906` と `brw_reg_type.c` と照らしました。opcode、access mode、NoDD、nib・qtr、thread ctrl、pred、exec size、cond mod・SFID 27:24、acc・branch 28、cmpt・debug、saturate、flag 32・33、NoMask 34、dst・src0・src1 の各 field、EOT 127、desc の 126:96、Gen9 の register の type と immediate の type の値は一致しました。

抜けているのは次の 2 点です。
- plain の SEND での ex_desc の重なり（M6）
- SENDS の src1 の file の bit 36

## HAL について

HAL の API の変更は要らない、という §9 の判断に同意します。i915 は既存の `hal_space_map_device` と drv_* だけを使います。新しい boot の key（`include/kern/boot.h`）は kernel の header で、HAL ではありません。ただし担当の表には入れる必要があります。

## ユーザーの判断が要る点

- **U1**: Gen9 の PCI match を既定で有効にするか。Beta 2 では boot の指定で有効にする形（既定は off）を勧めます（M8）。
- **U2**: Gen9 で display が absent の時の suspend と蓋を閉じた時の扱い。suspend を拒否するか、黒い画面を許容するか（M7）。
- **U3**: Beta 2 に p002 の refactor（device・irq・submit・request）を入れる条件。5330 の T1 での回帰を必須にするか（M6）。今日の code freeze に間に合わない可能性があります。

Q1 の範囲で決められる点:
- F2 で SENDS を使う方針への変更
- 他の WS の plan/tests の編集の許可（M2）
- §5 の担当と依存の表の直し

## 確かめられなかったこと（推測として扱ってください）

- F1 の semaphore が実際に hang するか（PRM の bit の割当てからの推測）。
- M3 の GT2 で RPCS が 0 の時の挙動。
- M4 の null state を省いた時の hang の有無。
- M8 の compositor の fallback の実際の挙動。

いずれも実機（p005）でしか確定できません。

## 効く Phase のまとめ

- **p002**: F1、M1、M2、M3、M4、M7、M8、M10、中 1〜3・6
- **p003**: F2、M5、M6、中 4
- **p004**: M2、M9、中 8、video の gate
- **p005**: M7、中 7

