<!-- awesome-plan project=zedbsd record=ws242-design-review -->

# WS242 design.md の敵対的レビュー

Parent: [design.md](design.md)。2026-10-29 design-reviewer（P5 の依頼）。照合した tree: worktree p5-design の HEAD d69da270a（`git diff 61630bb0b HEAD -- src/drivers/gpu/nvrtx` は空なので、design.md の照合の tree と nvrtx は同じ）。
公開の source は P5 の scratchpad の写し（信頼しない data。読むだけで、何も実行していない）。製品の表の照合にだけ自分の script（scratchpad、`python3 -I`）を使った。

重大度: **blocker**（このままでは受け入れ条件を満たせないか、壊れる）／**major**（直さないと誤りが残るか、試験で見つからない）／**minor**（文書・形・見積もり）。

## 要点

| # | 重大度 | 一行 |
| --- | --- | --- |
| B1 | blocker | §3 の失敗の段の表が code と違う。FRTS の後の失敗では teardown が走らず、gsp-init の失敗は `FAILED_HOLD`（memory を保持、console を戻さない、power cycle）。「段ごとの諦め」の道は今は無く、DMA を止める設計も無い |
| M1 | major | compute の launch の `SET_OBJECT` が `NVRTX_CLASS_COMPUTE`（0xc5c0）の固定。§2.2・§2.3 と試験に無い |
| M2 | major | p005（既定で起動）が p003 にしか依存しない。p004 より先に merge されると RTX 30/40 が既定で SM75 の code と Turing の 3D class で起動する |
| M3 | major | 起動の判断（family × firmware の画面 × `nvrtx.gsp`）と失敗の段の戻り方の試験が無い。受け入れの条件（§0.4）を確かめる手段が計画に無い |
| M4 | major | QMD・PCAS の host 試験の期待値が実装と同じ人の手写しになる（自己参照）。gsp-host-test の実 firmware の path が古く、GA/AD/TU116 の項が黙って飛ばされる |
| M5 | major | 戻す試験の置き場が `plan/ws242/tests/`（WS の完了でまた消える）。`plan/tools/nvrtx/nvrtx-vk-host-test.c` の stub が計画に無い |
| M6 | major | Q242-2 の推し（WS305 の VFIO）は止めた WS に依存し、passthrough の GPU の画面は QMP の screendump に写らない。cold reset も要る |
| M7 | major | Q242-1 の危険の書き方が狭い（`FAILED_HOLD`・WPR2 の残り・laptop の ACPI の data 無し）。選択肢が足りない |
| m1〜m9 | minor | §2.1 の前提と量の定義、carve-out の欄、vk-execute の変更の幅、層の境界、HBLANK_DELAY の gate、`boot_default`、記録の形、見積もり |

## B1（blocker）§3 の「失敗の段と戻り方」が code と違い、諦めの道が無い

根拠（`src/drivers/gpu/nvrtx/gsp.c`、`boot.c`）:

- `gsp_teardown()`（gsp.c 1112〜1170 行）: region（WPR2）を壊すのは「GSP が走っていて `gsp_stop()` で止まった後」だけ（`if (stopped && gpu->region_written != 0) error = nvrtx_region_tear_down(gpu);`）。GSP が走っていない時は「unload left: region …, no GSP started; reboot the host」を出すだけ。comment に「booter_unload は FWSEC-FRTS だけが作った region を拒む（mailbox 0x29）」とある。→ §3 の 2 行目「devinit・FRTS: teardown（FWSEC-SB・booter_unload）」は誤り。devinit の失敗では WPR2 は立たず、FRTS の失敗では WPR2 が残る（power cycle か host の reboot まで）。
- FWSEC-FRTS の timeout（gsp.c 1030〜1034 行）、booter_load の timeout（1296〜1300 行）、**gsp-init の INIT_DONE が来ない**（1370〜1374 行）はどれも `NVRTX_GSP_FAILED_HOLD`。`gsp_teardown()` は `FAILED_HOLD` で即 `EBUSY`（1121 行）なので、GSP falcon の reset・FWSEC-SB・booter_unload・`nvrtx_console_restore()` のどれも走らない。detach も `EBUSY`（nvrtx.c 216 行）。→ §3 の 3 行目「booter_load の後、INIT_DONE が来ない: teardown」は誤り。今の code は「memory kept for a dump; reboot the host」で、開発の dump のための道。
- booter_load が timeout 以外で失敗、または RISC-V が走らない（1302〜1312 行）は state が `REGION_SET` のまま `gsp_finish()` → `stopped == false` なので region は残る。
- 設計の「ogkm・nouveau の unload は GSP falcon の reset → FWSEC-SB → booter_unload、nvrtx の boot.c:303-340 も同じ順」: nvrtx では falcon の reset は `gsp_stop()`（unload の RPC と `GSP_UNLOADED` の mailbox の後）にあり、boot.c 303〜340 行は FWSEC-SB → booter_unload だけ。順は合うが、**unload の RPC に答えない GSP（init が終わらなかった GSP）にはこの道が無い**。

起こる状況: 既定で起動した実機の無い family（TU11x・GA10x・AD10x）で INIT_DONE が来ないと、GPU は `FAILED_HOLD` で保持され、GSP の RISC-V が動いたまま（推測: 途中まで初期化した GSP-RM）、system memory の DMA の buffer（radix3 の image・queue・log）は解放されず、console の window は firmware の mode に戻されない。§0.4 で受け入れ条件にした「段ごとの失敗で firmware の画面に戻す道」は今の code に無く、design.md はそれを作る設計も持たない。

DMA: `FAILED_HOLD` が memory を保持するのは、止まっていない falcon・GSP が system memory を読み書きしうるから（gsp.c の comment「Memory a falcon may still read stays until the power is cut」）。諦めの道で memory を解放するなら、先に bus master を切る（`drv_pci_device_set_bus_master(…, false)`、FRTS の前に true にしている gsp.c 1023 行の逆）か、GSP falcon を reset して止まったことを確かめる必要がある。どちらの順にするか、bus master を切った後に FWSEC-SB・booter_unload（どちらも falcon の DMA を使う）が走れるのかを決めていない。

直し方の案:

1. §3 の表を今の code の事実に書き直す（段ごとに state・region・memory・console の行方）。
2. 「既定で起動」の family のための諦めの道を設計する。案: (a) 今の `FAILED_HOLD` を保ち、画面は「firmware の display に GSP が触っていなければ残る」ことを実機で確かめるまで既定で起動しない（Q242-1 の選択肢）、(b) 実機の無い family では INIT_DONE の timeout で GSP falcon を reset（`nvrtx_falcon_reset`、止まったかを status で確認）→ 止まれば FWSEC-SB・booter_unload → `nvrtx_console_restore()` → memory の解放、止まらなければ bus master を切ってから memory を保持、の順を決める。(b) は新しい code で実機で試せないので、host 試験（state 遷移の表）と RTX 2070 での故意の失敗（下の M3）で確かめる。
3. 見積もり（p005 の 4 LW）を直す（m9）。

## M1（major）compute の launch の SET_OBJECT が Turing の class の固定

根拠: `compute-launch.c` 239 行 `nvrtx_put32(push, at, NVRTX_CLASS_COMPUTE);`（`nvrtx-compute.h` 33 行 `0xc5c0U`）。object は `compute.c` 688 行で `profile->class_compute`（GA 0xc7c0、AD 0xc9c0）で作る。§2.2 の field の表と §2.3 は version・shared・barrier・PCAS2_B だけで、この語を挙げていない。§4 の「PCAS と PCAS2_B」の項もこの語を見ない。

起こる状況: Ampere・Ada で `nvrtx.compute=1` の compute-test が subchannel に別の class を bind しようとして GR の例外（推測: illegal class の error で channel が RC される）。既定の道（vk）は compute を launch しないので既定の起動では出ないが、`nvrtx_push_compute_launch_for()` を作る時に見落とすと p004 の host 試験も通ってしまう。

直し方: §2.2 に「`SET_OBJECT` の class は profile の `class_compute`」を明記し、§4 に「launch の push の語の全部（`SET_OBJECT` の data・shared/local window・invalidate・`SEND_PCAS_A`・`SEND_SIGNALING_PCAS_B` か `PCAS2_B` の 0x02c0 と値 3）を TU10x・GA10x・AD10x の 3 つで」を足す。Turing は今の `check_launch_push()`（`plan/tools/nvrtx/nvrtx-sm75-host-test.c` 1203〜1227 行）の語と byte で同じ。

## M2（major）Phase の依存: p005 が p004 を待たない

根拠: §5 の表で p005 の依存は p003 だけ。既定の起動では vk が on（`stage.c` の `nvrtx_stage_vk_wanted()` は `nvrtx.vk=0` が無ければ true、`nvrtx_stage_work_wanted()` も true）なので、desktop の shader は `vk/vk-pipeline.c` 676 行・`vk/vk-execute.c` 412 行の SM75 の compile と、`vk-execute.c` 453 行の `nvrtx_push_draw_init()`（0xc597 の固定、`draw-push.c` 375 行）を通る。p002 §5 の記録どおり、SM75 の code は SM86・89 では memory order の欄と latency が違い「そのままでは正しくない」。

起こる状況: Q1 は担当の成果を順に main へ merge する。p005 が p004 より先に入ると、main の既定の起動で RTX 30/40 が GSP を起動し、SM75 の code と Turing の 3D class で desktop を描く（3D object は c797/c997 で作られ、push は c597 を bind する → class の不一致の例外、推測）。

直し方: p005 の依存を「p003・p004」にする。既定で起動に変える 1 行（`nvrtx.c` 168〜170 行の条件）は p005 の最後の commit にする。

## M3（major）起動の判断と失敗の段の試験が計画に無い

根拠: 起動の判断は `nvrtx.c` の attach（160〜186 行、PCI と stage の parameter を読む）にあり host で試せない。§4 の表は profile の値・layout・QMD・compiler・build・RTX 2070 の回帰だけで、(1) family × `gop_on_gpu` × `nvrtx.gsp`（無し・0・1）の組の結果、(2) 段ごとの失敗で画面がどうなるか、を確かめる項が無い。30/40 は実機が無いので、この 2 つは host と RTX 2070 でしか確かめられない。

直し方:

1. 判断を純粋な関数（例: `nvrtx_boot_decision(profile, gop_on_gpu, wanted, forced)` → keep／leave と印の種類）に出し identify.c か新しい file に置いて、identify の host 試験で全部の組を見る。
2. RTX 2070 の p006 に「段ごとの止め」を足す: `nvrtx.stop=layout`・`devinit`・`frts`・`booter`・`static-info` で起動し、画面（login prompt か firmware の console）が残ることを撮る。B1 で新しい諦めの道を作るなら、それを故意に通す仕組み（試験だけの boot parameter で INIT_DONE の待ちを 0 にする等）と、その時の画面・次の warm reboot の画面を確かめる。

## M4（major）host 試験が誤りを見つけられない所

1. **QMD の bit の位置**: §4「QMD 2.2 と 3.0 の bit の位置（open-gpu-doc の値と比べる）」は、実装の #define と試験の期待値を同じ人が同じ header から手で写すので、読み違い（例: 7 bit → 6 bit、955 → 763）が両方に入ると通る。案: header（`clc3c0qmd.h`・`clc6c0qmd.h`・`clc9c0qmd.h`）から field 名 → (high, low) を機械的に抜く小さな script で期待の表を作り、その表（出典の版と sha256 付き）を試験の入力にする。抜く field は nvrtx が書く 22 個と「書かないが 0 であるべき」物（`SHARED_ALLOCATION_ENABLE` 663、`SAMPLER_INDEX` 382、V02_02 の BARRIER_COUNT の跡 955〜959）。
2. **Turing の byte の一致**: `nvrtx_qmd_build_for(…, tu10x)` と今の `nvrtx_qmd_build()`、`_launch_for(…, tu10x)` と今の launch が同じ入力で byte で同じことを明記して試す（§4 の「QMD 2.2 が今と同じ」は今の 1 つの入力だけ）。shared memory が 0 でない入力（32 KiB・48 KiB・64 KiB、warps の多い workgroup）も入れる。今の試験は shared 0 の 1 例だけ（`nvrtx-sm75-host-test.c` 1184 行）。
3. **実 firmware の path**: 消えた `nvrtx-gsp-host-test.sh` の既定は `build/sources/firmware/nvidia-gsp/dc85ccedc9c973682fbcf4d628ca61174bcc3120`（`git show 41a28534b^:plan/ws037/tests/nvrtx-gsp-host-test.sh`）で、無ければ引数なしで走る（`if [ -d "$firmware" ]`）。GA/AD/TU116 の file は新しい cache `dc85cced…-tu-ga-ad` にしか無い（main の checkout の `build/sources/firmware/nvidia-gsp/` に両方ある、`userland/firmware/nvidia-gsp/Makefile` 91 行 `NVIDIA_GSP_FIRMWARE_SET := tu-ga-ad`）。path を直さないと §4 の「booter の meta と fuse（実 file があれば ga102・ad102・tu116）」は黙って飛ばされ、PASS と区別できない。案: 既定の path を新しい cache に、file が無ければ「SKIPPED n」を数えて出し、p003 の合格は「SKIPPED 0」とする。worktree の `build/` に cache は無いので、main の `build/sources` を読み取り専用で指す手順も書く。
4. **§2.1 の境界**: 255・256・257 MiB の項は、量の定義（m1）を決めないと実装と試験が同じ解釈で合ってしまう。期待値は ogkm の式（`fbSize − gspFwRsvdStart`、gspFwRsvdStart は non-WPR の heap の始まり、`kernel_gsp_tu102.c` 660〜663・779 行）から書く。

## M5（major）戻す試験の置き場と、止まる既存の試験

1. design.md は 3 本を `plan/ws242/tests/` に戻す。AGENTS.md §5 の「WS の完了」は、その WS の Phase の directory と「その WS だけの試験」を消す。3 本は nvrtx 全体の回帰（GSP の record・layout・vbios・identify・Vulkan の D3）で、WS242 の後も使う。同じ消え方を繰り返さないよう `plan/tools/nvrtx/` に置き `plan/tools.md` に登録する（`plan/tools.md` は Q1 の物なので Q1 に頼む。§8 に足す）。
2. p004 の owner の口（`nvrtx_vk_shader_model`・`nvrtx_vk_threed_class`）は `plan/tools/nvrtx/nvrtx-vk-host-test.c`（`nvrtx_vk_device_id` などを stub で持つ、88〜99 行）を link で壊す。§4 にこの試験の追従が無い（p001 再開点 1 には書いてある）。stub は SM と class を変えられる形にし、vk の道で 86・89 の compile も 1 回通す。
3. 消えた 3 本が `plan/tools/` の外にあった事は、§9「参照されていれば消さない」より、未登録だった事が根の原因。報告にそう書くと Q1 の直しが決まる。

## M6（major）Q242-2: RTX 2070 の回帰の機械と判定

根拠: WS305（`plan/ws305/ws.md`）は Status incomplete で master.md 29 行の「止めたまま」。p006 の依存に「機械（Q242-2）」とあるが、推しの案は止めた WS の完成に依存する。

起こる状況:

- VFIO の guest では zedBSD の画面は passthrough の GPU の出力端子に出て、QEMU の display（boot-test.sh・QMP の `screendump` が撮る物）には写らない。AGENTS.md §9 の判定の方法で、この形の判定の例外は無い。WS037 の受け入れ（display・desktop）と同じ確かめにならない。
- guest の OVMF が GPU の GOP を走らせるかで `gop_on_gpu` が変わり、既定の道（firmware の画面のある GPU で起動）を通らないことがある（推測、実物で未確認）。
- nvrtx の失敗の多くは「power cycle needed」（B1）。VFIO の reset で WPR2 が消えるかは未確認。ユーザーも「コールドリセットが必要かも」（ws305/ws.md の出典）と書いている。

直し方: 推しを「素の起動（10.0.10.3 の GPU を RTX 2070 に差し替え、BootNext と SwitchBot の cold reset）で T1、画面は撮影か HDMI の capture、もしくはユーザーが拾える試験の一覧」にし、VFIO は WS305 が再開して判定の方法が決まってからの選択肢にする。WS291 の P2 が同じ机を使う事を §6 に書く。

## M7（major）Q242-1 の危険の書き方が狭い

design.md の Q242-1 は「画面が出なくなるかは公開の source で分からない」だけを危険に挙げる。code の事実から足すべき物:

- B1 のとおり、gsp-init の失敗で GPU は `FAILED_HOLD`（DMA の memory を保持、GSP が動いたまま、detach 不可）。region は host の reboot か power cycle まで残る。次の warm reboot で nvrtx は「frts stop: region … already set; power cycle needed」（gsp.c 1017 行）で諦める。firmware（GOP）が次の POST で画面を出せるかは未確認（推測）。`nvrtx.gsp=0` は次の起動からの逃げ道で、今の起動で失われた画面は戻さない。
- 製品の表 288 個のうち laptop が 115 個（`products.c` の laptop の欄）。nvrtx の GSP_SYSTEM_INFO は ACPI の data を書かない（`records.c` 111 行「no ACPI data」）が、nouveau は書く（`nvkm/subdev/gsp/rm/r535/gsp.c` 882・922 行 `r535_gsp_acpi_info`）。MUX のある laptop で firmware の画面が dGPU にあると既定で起動する。その時に GSP-RM が ACPI の data なしで init を終えるかは公開の source で確かめていない（推測の危険）。

直し方: Q242-1 の選択肢に「既定で起動は desktop の製品（laptop=0）だけ、laptop は `nvrtx.gsp=1`」と「B1 の諦めの道ができるまで `nvrtx.gsp=1`」を足し、上の事実を Q1 経由でユーザーに渡す。ユーザーの決定（2026-10-15「GTX 16・RTX 30・40 はデフォルトで有効にします。」）を変える提案ではなく、決定の時に無かった事実の報告として書く。

## minor

- **m1 §2.1 の前提と定義**: (a)「GOP（firmware）が scrub 済みにする」とあるが、ogkm は「Firmware scrubs the last 256mb of FB」（`kernel_gsp_tu102.c` 642〜643 行）で、256 MiB は chip の HAL で TU/GA/AD に一律（`g_kernel_gsp_nvoc.c` 1005〜1007 行）。GOP（UEFI の driver）の有無で変わるとは書いていない。GPU の起動時の firmware（devinit）が行う物と読むのが自然で、§3「GOP が走った GPU の前提」「headless・二次の Ada は諦める」の理由にはならない（結論の「gop_on_gpu の条件を変えない」は別の理由で正しい）。scrub を誰がするかは公開の source で確定していない、と書く。(b) 量は「VRAM の末尾から WPR の始まり」ではなく「末尾から外の heap（non-WPR heap）の始まり」（ogkm の `neededSize = fbSize − gspFwRsvdStart`、`kernel_gsp.c` 3335 行、nvrtx では `layout->outer_heap`）。(c) 失敗の印を「VRAM too small」（gsp.c 709 行）と分ける。(d)「ELF 63.5 MiB」は 63,571,696 byte = 60.6 MiB（Makefile 37 行）。layout が使うのは `.fwimage` の大きさ（`file.c` 301 行）で、file よりやや小さい。余りの見積もり（約 20 MiB）は heap 170 MiB の時に 21〜24 MiB で、結論は変わらない。
- **m2 carve-out と warps**: profile に `warps_per_sm`・`shared_kib[]`・`shared_count` がある（`nvrtx-private.h` 177〜181 行、`identify.c` の GA/AD は 48 と {0,8,16,32,64,100}）が、`compute-launch.c` は Turing の定数（72〜75 行 32/64 KiB・32 warps、155 行の上限 64 KiB）を使う。§2.2 は使うかどうかを書いていない。Turing の値は Ampere の carve-out の集合の中にあるので正しさは保つ（速さだけ）。使うなら §2.2 に MIN・TARGET・MAX の求め方（Mesa `qmd.rs` 365〜412 行、MAX は最後の carve-out: Ampere 100 KiB → 26）と上限の検査を書き、使わないなら欄を消す。
- **m3 vk-execute.c の変更の幅**: §6「vk-execute.c の変更は呼び出しの行だけ」とあるが、`execute_compile()`（189・400 行）と `execute_start()`（190・444 行）は session も owner も受けない static の関数で、引数を足す（呼び手 380・383・244 行も）。`vk-pipeline.c` の `pipeline_compile_stage()`（73・666 行）も同じ。WS291 の P2 との衝突の範囲をこの関数まで広げて Q1 に伝える。
- **m4 層の境界**: `nvrtx_qmd_build_for(qmd, info, profile)` は `compute-launch.c`（hardware に触らない部品）に `struct nvrtx_profile`（`nvrtx-private.h`、`<drivers/pci/pci.h>` を include）を持ち込み、`nvrtx-sm75-host-test.sh`（compute-launch.c と draw-push.c を compile する）にも広がる。class・QMD の版・warps・carve-out だけの小さな記述（`nvrtx-compute.h` の型）を渡し、profile からの詰め替えは compute.c 側に置く方が境界が保たれる。
- **m5 HBLANK_DELAY**: `HEAD_SET_RASTER_HBLANK_DELAY`（0x2364）は c67d・c77d にあり c57d に無い（`clc57d.h` は 0x2360 の RG_MERGE で終わる）。class で必ず分け、Turing の head の push が byte で変わらない試験を足す。また 0 を書くより firmware の armed の値を写す方が、HDMI の DSC の mode を GOP が組んだ時に安全（nvkms は HDMI の DSC で 0 以外を書く、`nvkms-evo3.c` 1566〜1585 行）。
- **m6 `boot_default`**: 4 つの family が全部 1 なので 0 の枝は通らない code になる。残すなら将来（WS240）のためと書く。`identify.c` の record は位置の初期化子（78〜111 行など）なので、欄は末尾に足すか designated の初期化子に替える。`nvrtx.c` 19 行・104 行と `nvrtx-private.h` 127〜131 行の comment（verified の意味）も直す。
- **m7 記録の形**: (a) p001 を「済み（実機なし）」で閉じる案は状態の語に無い（AGENTS.md §2）。p001 は範囲（起動・RM・class・QMD の差）を満たしていないので、`uncleared`（残りを p003〜p005 へ移す）と書く。p002 も phase.md は `in-progress` のままで、design.md の「済み」と合わない。(b) §2.1 の境界の試験が p003（「layout の §2.1」）にあり、実装は p005（「§2.1 の検査」）。p003 の時点では試す物が無い。実装と試験を同じ Phase（p005、または p003 に実装も入れる）にする。(c) §4 の「287 checks・138 compiled が今と同じ」は、QMD 3.0 の項を `nvrtx-sm75-host-test.c` に足すと数が変わる。足す file を決めて合格の数を書き直す。(d) p005 の「cache の `<rev>-tu-ga-ad` の fetch」は main の `build/sources/firmware/nvidia-gsp/dc85cced…-tu-ga-ad` に既にある（中身の照合は未実施）。
- **m8 WS250 R10**: §5 は「p004 で確かめる」だけで、衝突した時の扱いが無い。調べた範囲では、nouveau の GSP の道は ga1xx・ad10x も `tu102_chan_doorbell_handle`（runlist id << 16 | chid、`rm/ga1xx.c` 26 行・`rm/ad10x.c` 26 行）で nvrtx の doorbell（`channel.c` 854 行、`compute.c` 342 行）と同じ形。chid は runlist ごとの空間なので、work 4（graphics）と copy 8（copy の runlist）は衝突しない。危険は低いと書き、確かめの結果が違った時は「session の chid を graphics の runlist の chid の上限の中に」とだけ決めておく。
- **m9 見積もり**: B1 の諦めの道を作るなら p005 は 4 LW では足りない（state の表・bus master・falcon の reset の確認・host 試験・RTX 2070 での故意の失敗の手順で 6〜8 LW の見込み）。p006 は M6 の機械の準備（GPU の差し替え）を含めない形で書く。

## 確かめて正しかった事

- §0.1 の行: `draw.c` 443・568、`compute.c` 736・811・820、`vk/vk-pipeline.c` 676、`vk/vk-execute.c` 412・453、`compute-launch.c` 261 は全部合う。compile の呼び手はこの 4 か所だけ（compiler の中の wrapper を除く）。
- QMD V02_02 → V03_00 の位置（open-gpu-doc `clc3c0qmd.h`・`clc6c0qmd.h`・`clc7c0qmd.h`・`clc9c0qmd.h`）: version 579:576・583:580、MIN 567:562・MAX 574:569・TARGET 662:657（6 bit、663 は SHARED_ALLOCATION_ENABLE）、BARRIER_COUNT 767:763、caching 134、call limit 378、raster 384・416・448、thread 592・608・624、cbuf valid 640+i、REGISTER_COUNT_V 656:648、cbuf の address・size 1024+64i・1056+64i・1075+64i、program 1536・1568。clc9c0 の V03_00 も同じ位置。Mesa 25.3.6 `qmd.rs` は c7c0・c9c0 にも Qmd3_0 を使う（659〜709 行）。SAMPLER_INDEX の INDEPENDENTLY は 0 で、nvrtx が書かない事と合う。
- PCAS2_B: `clc6c0.h` 372〜377 行（0x02c0、値 3）、Mesa `nvk_cmd_dispatch.c` 329〜337 行（`<= TURING_COMPUTE_A` で PCAS_B、それ以外で PCAS2_B）。
- §1 の表: WHENCE の link（tu116 の image・bootloader → tu102、ad102 の image → ga102）、`fb/ga102.c` 29〜32 行、`devinit/ga100.c` 66〜73 行、`bios/shadowramin.c` 67〜68 行、`rm/r570/gr.c` 95〜103 行、`dispnv50/core.c` 46〜48 行、ogkm `gsp_fw_heap.h`（OS 22 MiB、LIBOS3 の min 88 MiB、別の build の形で 170 MiB）、`kernel_gsp.c` 3330〜3345・5096〜5107 行、Ada だけ scrubber の ucode を持つ（`g_kernel_gsp_nvoc.c` 385〜394 行）、nvkms `EvoSetRasterParamsC6`、Mesa `nouveau_device.c` 116〜135・202〜215 行。
- 製品の表: `products.c` の 288 個は scratchpad の pci.ids（Version 2026.10.28、sha256 41f814a4…）に全部あり、TU/GA/AD の範囲で表に無い id は audio・Orin・GH100・GB の機能だけ。pci.ids の header は GPL-2.0 以上か BSD-3-Clause の選択（13〜14 行）。`products.c` の表に「Values referenced:」の block がある（31 行）。
- GR の context buffer: RM が返す大きさで作り、RTV_CB_GLOBAL も表にある（`objects.c` 213・281 行）。設計の「chip で分けなくてよい」と合う。
- HAL の API・UAPI・ABI の変更は不要（nvrtx の内側だけ）。新しい lock も要らない（owner の口が読む profile は attach の後は定数）。

## 未実施

- 実機・QEMU の試験（この依頼の範囲の外）。build と host 試験は走らせていない。
- 消えた 3 本の試験が今の API で compile するかの確認（p003 の仕事）。
- GSP-RM の init の途中の失敗で GOP の scanout と BAR1 がどうなるか、VFIO の reset で WPR2 が消えるか、laptop で ACPI の data なしに init が通るか（どれも公開の source で決まらない、推測として上に書いた）。
