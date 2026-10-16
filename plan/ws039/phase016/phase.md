<!-- awesome-plan project=zedbsd record=ws039-p016 -->

# ws039-p016: `reset`（mode1 reset ＋ asic_init ＋ A2〜A5s の設定し直し ＋ re-light）と `sos alive` の自動の reset

Status: in-progress（2026-10-17 01:18 P3: P3 の branch に merge（5c17b43cf）、実機 R1 の 1 回目で直した 2 点（c95be47f8・c4e3099a0・6c26527d9）の後、R1 の reset 3 回が合格。R2・R3 は未実施。merge と clearance の判定は Q1）
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1237 / q1237-i01（P3 の p015 と並行。2026-10-16 ユーザー「P1,P3の処理で、並列にオフロードできる実装があれば、それぞれ1エージェントずつ追加してほしいです。…並列な実装は完璧でなくてもよく、あとでマージするときに整合が必要でもOKです。」）
承認: 2026-10-16 ユーザー「p013,p014,p015,p016を現在のサブエージェントで継続してください。」と上の並行の指示（Q1 の q1237）
実行者: P7（worktree `/home/awe/zedBSD-worktrees/p7`、branch `agent/p7-ws039`、base `8a580e906` = P3 の `agent/p3-ws039`）

## 範囲（[design.md](../design.md) §16 の p016 の行、§11.6、§15 #10「案のとおり（後で）」）

`reset`: 停止の手順 → mode1 reset → asic_init（A10 の AtomBIOS の runner）→ A2〜A5s の再実行 → re-light（P3 の A11 の relight.c）。warm reboot の後などで A0 が `sos alive` を見た時の自動の reset（cold power cycle を無くす）。P3 の mode set の file（relight.c・present.c・sink.c・dcn の register・modeset.c）は呼ぶだけにする。

## 設計（実装した形）

- **走らせる所**: reset は bring-up の thread の上で段と段の間に走る（段と並ばない）。頼む口は 3 つ: (1) 起動の A2 の前に A0 が `sos alive` を見た時（`amdrdna.reset=1` の時だけ。無ければ従来どおり A1v で hold）、(2) root の `amdrdnactl reset`（control の node の `AMDRDNA_IOC_RESET`、GPU device の session と display の lease が無い時だけ、EBUSY で断る）、(3) GPU core の回復（`drv_gpu_recovery_ops.reset`、`amdrdna.reset=1` の時だけ op を入れる。無ければ従来の NULL の表で挙動は変わらない）。
- **順**（`reset.c` の `reset_run`）: system-memory table（GART、VRAM の 1 MiB）を kernel に写す → display の pipe の register を読む（下）→ 動いている物を止める（renderer を hold、A5s 以降は空間の run を拒む、A5 の engines の停止、A4 の power の停止（SMU の unload）、A3 の PSP の ring の破棄、fault の割り込みと IH の ring を止める。thread と確保済みの buffer は残す）→ **mode1**（PSP の ring control の語が ready を待ち、BIOS_SCRATCH_3 の hung の bit、PCI の状態と resizable BAR の control の語を保存、bus master を切る、C2PMSG_64 = 0x70000、500 ms、resizable BAR の語を戻してから PCI の状態を戻す、bootloader の ready（C2PMSG_35）、VRAM の大きさの語が全 1 でなくなる、SOS の語が 0）→ **asic_init**（C2PMSG_33 の ready を待ってから、firmwareinfo の boot の engine・memory の clock で command の表の 0 番を runner で。parameter は Linux と同じく 72 dword の範囲を 0 で）→ 起動の reset なら display を戻して終わり（後は普段の A2 から）。実行時の reset なら **A2 の設定し直し**（`amdrdna_a2_restart`: GART の写しを書き戻し、hub の語を記録し直して両 hub を設定、HDP・fault・doorbell、TLB、hub の読み戻し、IH の ring の設定し直し）→ display を戻す → A3t は走らせない（post が memory を初期化）→ **A3**（`amdrdna_a3_psp` をそのまま。buffer は「record の寿命の間 1 回」の作りで使い回し、中身は書き直す）→ **A4**（`amdrdna_power_rearm` で mailbox と feature の flag を戻してから `amdrdna_a4_power`）→ **A5**（`amdrdna_a5_engines` をそのまま、同じく使い回しの作り）→ **A5s**（空間が残っていなければ context の pool を初期化して run を受ける。session の検査は走らせない）→ renderer の hold を外す。
- **VRAM は失われる前提**（Linux の `amdgpu_device_check_vram_lost` は mode1 を常に lost と扱う）。GART の表は写しから戻す。PSP の ring・TMR・clear state・SMU の table・ring の buffer は各段がもう一度書く。session の物は reset の時点で無い（root の口も core の口も session が無い時だけ）。
- **display**（`reset-display.c`）: reset の前に firmware の pipe の register（`reset-format.c` の 128 個の名前付きの語と 13 個の別の語: hub の watermark、plane の surface・viewport・DLG/TTU、DPP・scaler・cursor、MPC、OPP、OTG の timing（DCCG の pixel rate・DTO を含む）、DIG の FE/BE の source、DCN_VM の FB と AGP の窓、DCCG の gate、OPP の pipe の clock）を読み、OTG の frame counter で refresh を測って pixel clock を出す（A10 の EDID に同じ total の mode があればその pixel clock）。post の後: VBIOS の EnableDispPowerGating（pipe 0 に INIT、plane の pipe の gating を off。Linux の `dcn10_bios_golden_init` の順）→ 語の書き戻し（OTG の master enable と VTG の enable は切って）→ VBIOS の SetPixelClock（PHY の PLL、P3 の A12 と同じ値の組み方）→ VTG と OTG の起動 → driver が持っていた display の割り込み（VSTARTUP・flip・HPD）を入れ直す → underflow の bit を消す → A10 が未実行なら `amdrdna_a10_sink` → **P3 の `amdrdna_a11_relight`**（encoder の stream と transmitter）→ 30 frame と underflow で確かめる。display が戻らなくても GPU は動かし続ける（印は `reset note: the display stays dark`）。
- **失敗**: mode1 の前の失敗（GART の写し、SOS が答えない、PCI の保存）は GPU に触らずに戻る。mode1 の後の失敗は GPU を fault のまま（node があれば `drv_gpu_report_error`）。次の起動（warm reboot）で GOP が戻る見込み（この機械は warm reboot で PSP・SMU が reset される、p004 の 2 回目の観測）。

## 実装（2026-10-16 23:58〜2026-10-17 00:2x P7、commit 9d8c0b871）

- 新しい file: `src/drivers/gpu/amdrdna/reset.c`（上の順、要求と thread の受け渡し）、`reset-display.c`、`reset-format.c`（post の parameter・版の検査・pixel clock・保存する register の表・OTG の語、host で走る）、`amdrdna-reset.h`（状態と宣言。stage の file に足した関数の宣言もここに集めた）、`userland/tests/amdrdnactl/reset.c`（`amdrdnactl reset [DEVICE]` → `RESET ok ms=… resets=…` / `RESET fail result=…` / `RESET refused …`）。
- 既存の file に足した物（P3 の mode set の file は触っていない）: `gmc.c`（`amdrdna_gart_save`・`amdrdna_a2_restart`）、`ih.c`（`amdrdna_ih_quiet`・`amdrdna_ih_restart`）、`smu.c`（`amdrdna_power_rearm`）、`space.c`（`amdrdna_spaces_restart`）、`runtime.c`（`amdrdna_runtime_hold`、reset 付きの recovery の表）、`amdrdna.c`（thread が reset の要求を受ける、A2 の前の `sos alive` の reset、`amdrdna.reset=1` で A1v の hold を外す）、`identify.c`（A0 の印）、`control.c` と `include/uapi/amdrdna.h`（`AMDRDNA_IOC_RESET` 6、`struct amdrdna_reset_ask` 32 byte）、`amdrdna-private.h`（include と `struct amdrdna_reset reset` の 1 欄）、`amdrdna-node.h`（1 行）、`platform/amd64/vmunix.mk`（別の ifeq の塊で 3 file）、`userland/tests/amdrdnactl/`（main・header・Makefile）、`plan/ws039/tests/audit-allow.txt`（extra・finished・reset、一般の英語）。
- boot の語: `amdrdna.reset=1`（既定 0）。実機で確かめた後に既定を 1 にするかは Q1・ユーザーの判断。

## host 試験と build（2026-10-17 00:1x〜00:2x、host だけ。実機・QEMU の証拠は無い）

| 試験 | 結果 |
| --- | --- |
| build `make -j16 BUILD=build/p7 ZEDBSD_CONFIG=plan/ws039/tests/config.mk build/p7/vmunix build/p7/bin/amdrdnactl` | rc 0、warning 0、`amd64 vmunix check: PASS`、kernel include check PASS |
| `plan/ws039/tests/amdrdna-reset-test.sh`（新、`AMDRDNA_LINUX` に P3 の temp の v6.19 の tree、rom.bin は P3 の temp の cold の dump） | PASS 11394 checks。post の parameter、版の検査、pixel clock、表（全部の語に Navi10 の address、状態・割り込み・CRC・I2C の語を含まない、重複なし、OTG の switch が timing の語の後、plane の control が最後、別の語が dump の名前の address と一致）、**post を実 VBIOS で driver の runner と Linux の atom.c で 4 つの register の model で走らせて記録が一致**。model 3（間接の窓の data の語が bootloader の ready）で post は 44 event・27 write・282 step で終わり、最後に BIOS_SCRATCH_7 に posted（0x200）を書く。他の model は最初の C2PMSG_33 の待ちで同じ所で切れる |
| `plan/ws039/tests/amdrdna-host-test.sh` | PASS（dcn 882・hub 479・engines 170、audit-symbols 60 file） |
| `plan/ws039/tests/amdrdna-vk-host-test.sh` | PASS 196 |
| `plan/ws039/tests/amdrdna-bytecode-test.sh` | PASS 475 |
| `git diff --check` | ok |

## 実機の試験の依頼（Q1 経由、P3 と時間を調整。P7 は実機に触らない）

**機械**: 10.0.10.3（RX 5600 XT、Debian 13、USB の zedBSD を `hw-run.sh boot 0001` で 1 回だけ起動）。P3 の p015 と同じ机・同じ monitor（DELL SE2426H、DP の connector に DP++ の受動の変換で DVI）。

**image**: `agent/p7-ws039` の 9d8c0b871 を、その時点の P3 の branch の先頭に merge した tree（Q1）で、`make -j16 BUILD=build/<who>/p16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk build/<who>/p16/vmunix build/<who>/p16/bin/amdrdnactl`。USB の image は今の物を使い、Debian から USB の sda2 の `vmunix` だけを差し替える（前の物は Debian の `~/ws039/` に残す。p005 以降と同じ手順）。`amdrdnactl` は zedBSD に上がってから scp で `/tmp/amdrdnactl` へ。zedbsd.cfg は今のまま（`amdrdna.auto=A9`、login=text、display は driver が持つ既定）。

**R1: 実行時の reset（主）**。desktop は起動しない（text login、GPU device の session が無い状態）。
1. `hw-run.sh boot 0001` → `AMDRDNA_ZED=root@10.0.30.5 hw-run.sh collect`。`amdrdnactl status` が `stage=A9`（以上）・`failed=none`、`dmesg | grep amdrdna` に A9 まで ok。
2. `/tmp/amdrdnactl reset` を 1 回。**合格**: 出力 `RESET ok ms=<数千> resets=1`、dmesg に順に `reset begin (request)`・`reset: display kept, 141 words, otg0 running, pclk=148500kHz`・`reset: mode1 now`・`reset: mode1 done, boot=0x80000000 vram=6128MiB`・`reset: post ok … posted=1`・`A2 again after the reset`・`A2 ok again`・`reset: display lit again otg0 hubp0 … underflow=0`（A11 の `A11 ok relit …` を含む）・`A3 ok`・`A4 ok`・`A5 ok`・`reset ok (request) in …ms`。
3. 続けて `amdrdnactl status`（fault=0 hung=0、fences が進む）、`amdrdnactl display`（frame・refresh が進む、underflow 0）、`amdrdnactl gpu`、`vkhang --loops`・`vkblend`（image にあれば。p013 と同じ道具）が ok。
4. 2〜3 を 2 回くり返す（resets=2・3）。
5. 画面（ユーザーの目視、急ぎでない一覧に載せる）: reset の間に一度消え、text console の画面が戻るか（VRAM が失われるので古い文字は崩れてよい。新しい行が出るか）。
6. `reboot` で Debian に戻る（shutdown の hook の行は disk に残らない）。

**R2: `amdrdna.reset=1` の起動**（任意、R1 が ok の後）: zedbsd.cfg の boot の行に `amdrdna.reset=1` を足して起動。A0 は `sos=0` のはずなので普段と同じ（A0 の `note: sos alive` の行は出ない）。`amdrdnactl reset` が R1 と同じく ok。core の回復の道（device の fault の後の fresh open）は fault を起こす手段が無く **未確認のまま**。

**R3: 起動時の `sos alive` の自動の reset**: この機械は warm reboot で PSP・SMU が reset されるので（p004）、起動時に SOS が居る状態を作れない。**未確認のまま**（同じ mode1・post・display の code を R1 が通る。順だけ違う）。

**読む物**: `kernel.log`/dmesg の `amdrdna:` の行、`amdrdnactl` の出力、写真（あれば）。T1 の結果 file に R1 の各回の `RESET` の行と `reset` の印の行を写す。

**失敗した時の戻し方**:
- `RESET fail` が出ても zedBSD の SSH が生きていれば: `amdrdnactl status` と dmesg を取ってから `reboot`（warm）で Debian へ。この机では warm reboot で PSP・SMU が reset され GOP が post し直す（p004 の 2 回目・p012 の warm reboot の観測）ので、Debian の画面は戻る見込み。
- 画面が暗いまま・SSH が 5 分戻らない（mode1 の後に PCI の戻しが効かず固まった等）: **電源の入れ直し**が要る。T1 は Q1 に報告し、Q1 がユーザーの一覧に「10.0.10.3 の電源の入れ直し」を載せる（今すぐは頼まない）。入れ直しの後は BootNext が 1 回限りなので Debian が立つ。
- Debian の起動で画面が出ない時も電源の入れ直し（GPU が中途の状態）。Debian で amdgpu は load しない（p015 の事故の後の Q1 の規則）。
- 次の zedBSD の試行の前は warm reboot で足りる（R1 の失敗の後でも、GOP が戻っていれば）。

## P3 の branch への merge と実機 R1（2026-10-17 00:33〜01:18、P3）

**merge**（5c17b43cf = 913cf2480 ＋ 3f9a8fcb5）: 衝突は `plan/ws039/tests/audit-allow.txt` の 1 行だけ（finished・firmware を整列して両方）。ioctl 6 番は重ならなかった。P7 の注意の点: `amdrdna_a11_relight` に pixel clock の引数（kHz、0 なら A6 が測った refresh から）を足し、`reset-display.c` は測った `pipe->pixel_khz` を渡す。`device->display.refresh_millihz` への書き込みは外した。build（`make -j16 BUILD=build/p3/usb ZEDBSD_CONFIG=plan/ws039/tests/config.mk build/p3/usb/vmunix build/p3/usb/bin/amdrdnactl`）rc 0・warning 0・kernel include check PASS・amd64 vmunix check PASS。host 試験 `amdrdna-host-test.sh`・`amdrdna-vk-host-test.sh`・`amdrdna-bytecode-test.sh`・`amdrdna-reset-test.sh`・`amdrdna-dml-test.sh` 全て PASS。

**実機**（10.0.10.3、RX 5600 XT、zedBSD を USB から。boot の行は text login・`amdrdna.auto=A9`、`hw-run.sh boot 0003`。電源の入れ直し無し、全部 warm reboot）:
- 1 回目（kernel 22f4d18c = 5c17b43cf、00:59〜01:01）: `RESET fail result=42 ms=26509 resets=0`。mode1 done・post ok posted=1・A2 ok again・`A11 ok relit … underflow=0/0`・display lit again・A3 ok・A4 ok まで進み、**A5 の `kiq ring test read 0xcafedead`**。原因: ring の software の `write_pointer` と `sequence` が 2 回目の A5 で 0 に戻らず（hw の queue は mode1 の後 0 から、ring の buffer は 0 で埋め直される）、doorbell で渡した write pointer まで 0 の語（type 0 の packet）を CP が実行していた。直し: `engines.c` の `engines_ring` で write pointer・sequence を 0、up を false に（c95be47f8）。
- 2 回目（c95be47f8 の kernel、01:04〜01:06）: RESET ok を 3 回。ただし `amdrdnactl display` の frame_irq・refreshes が reset の後止まる（OTG の frame counter は進む、OTG の sync status は INT_STATUS が立ったまま）。原因: post の後 display の割り込みを入れ直した時点では IH の ring が A3 まで off で、最初の VSTARTUP が配られず ack されないまま残り、次の割り込みが来ない。直し: A3 の後に display の割り込みを ack し直す（`amdrdna_reset_display_rearm`、c4e3099a0）。3 回目（c4e3099a0、01:11〜01:13）で frame_irq は 60/s で進んだが、refreshes が reset ごとに 2^24 跳ぶ（frame counter が 0 から始まる）→ refresh の数の base を取り直す（`frame_valid = false`、6c26527d9）。
- **合格の回**（6c26527d9 の kernel、vmunix cksum 1719841716、01:14〜01:18）: `amdrdnactl reset` を 3 回、毎回 `RESET ok ms=26774〜27315 resets=1/2/3`。dmesg: `reset begin (request)`・`reset: display kept, 141 words, otg0 running, pclk=148500kHz`・`reset: mode1 now`・`reset: mode1 done, boot=0x80000000 vram=6128MiB resizable=2`・`reset: post ok … posted=1`・`A2 again after the reset`・`A2 ok again … underflow=0`・`A11 ok relit 1920x1080@60.000 DVI … underflow=0/0`・`reset: display lit again otg0 hubp0 pclk=148500kHz frames=30 underflow=0`・`A3 ok`・`A4 ok … mode=F`・`A5 ok`・`reset ok (request)`。各回の後: status `failed=none`、`fault=0 hung=0`、display の frame_irq・refreshes が 1 s に 60 進む、`gpu ok`（2 回目の版）、`vkhang --loops`（`VKHANG ok … pixel=ff6f0000`）・`vkhang --compute --loops`（steps=111）・`vkblend`（`ok cases=20`）。証拠 `build/tmp/p16-r4/`（reset1〜3.txt・dmesg.txt）、1・2 回目は `build/tmp/p16-r1/`・`p16-r2/`・`p16-r3/`（P3 の worktree、commit しない）。
- 未実施: 画面の目視（reset の間に消えて text console が戻るか。ユーザーの一覧へ）、R2（`amdrdna.reset=1` の起動）、R3（起動時の `sos alive`、作れない）、core の回復の道。reset が 27 s かかる（内訳は未測定。A3 の firmware の load と A4・A5 の確かめが大きい見込み）。
- 道具の直し: UEFI の Boot0001 は古い USB の ESP を指していて（今の USB は Boot0003）、`hw-run.sh boot 0001` は検査を通って Debian に戻るだけだった（`/dev/disk/by-partlabel/` の escape が効かず PARTUUID が空で検査を飛ばしていた）。`blkid -t 'PARTLABEL=zedBSD EFI System'` で ESP を探し、見つからなければ断るように直した（Debian の上の実行は次の実機の時に確かめる）。

## P3 の branch と合わせる時の注意（merge の時に Q1）

- `amdrdna-private.h`（include 1 行と `reset` の欄 1 行）、`amdrdna.c`（thread・A2・gate。P3 は `AMDRDNA_LAST_STAGE` を A12・A13 に上げるはず。その行は触っていない）、`control.c`（switch に case 1 つ）、`include/uapi/amdrdna.h`（ioctl 6 番。P3 が別の ioctl を 6 番で足していたら番号を変える）、`platform/amd64/vmunix.mk`（別の ifeq の塊）、`userland/tests/amdrdnactl/main.c`・`Makefile`・`amdrdnactl.h`（P3 が subcommand を足していれば隣の行）、`audit-allow.txt`（整列の list）。
- **relight.c**: `amdrdna_a11_relight` は symbol clock を `device->display.refresh_millihz`（A6 が測る）から出す。A6 の前（起動時の reset）には 0 なので、`reset-display.c` が測った refresh を **0 の時だけ** そこに入れている。P3 の側で A11 が timing（または pixel clock）を引数で受ける形にしてくれれば、この書き込みは要らない（返り値として P3 に依頼したい点）。
- **modeset.c（P3 の A12）**: 今の reset の display は「保存した語の書き戻し＋SetPixelClock＋OTG の起動＋A11」の自前の手順。P3 の A12 の whole sequence は pipe が firmware に組まれている前提（HUBP の surface・DPP・MPC・OPP・DIG の mux・DCN_VM の窓・power gating は書かない）なので、そのままでは reset の後に使えない。A12 に「pipe を何も無い所から組む」入口ができたら、`reset-display.c` の `display_write_back`〜`display_start` をそれに置き換えるのが本筋。
- `display.c` の `display_interrupts` は static なので、`reset-display.c` に同じ形（display-format.c の公開の語の関数を使う）を写した。display.c に公開の入口を作れば 1 つにできる。

## 確かめていない物・残り

- 実機の全部（R1〜R3）。mode1 の後に PCI の構成（MSI・BAR）の戻しが十分か、resizable BAR を広げた（p017、8 GiB）BAR0 が戻るか、DCN の pipe の書き戻しで足りない register（DPP の CM・OPP の FMT・DCCG の DPPCLK の DTO・DCHUBBUB の他の語）が無いか、memory training（A3t）を走らせないで足りるか、`amdrdna_a3_psp`・`amdrdna_a5_engines` が 2 回目でも通るか（作りは使い回しだが実機は 1 回目しか通っていない）。
- core の回復の道（fault の後の fresh open で reset、`amdrdna.reset=1`）の実機、isolate で取り上げた VRAM（`backing->kept`）は reset でも返らない（記録が無い。容量の損だけ）。
- reset の間の text console の CPU の書き込み（BAR0 の decode が切れている間）は捨てられる見込み、未確認。
- license の監査の表（`plan/ws039/amdrdna-license-audit.md`）に足す出典（P3 の更新と重ならないよう Q1 の merge の時に）: `amdgpu/psp_gfx_if.h`（MODE1_RST）、`amdgpu/psp_v11_0.c`（mode1_reset・wait_for_bootloader）、`amdgpu/amdgpu_device.c`（mode1_reset・check_vram_lost・pre_asic_reset）、`amdgpu/nv.c`（reset_method・need_reset_on_init）、`amdgpu/amdgpu_atomfirmware.c`（asic_init）、`include/atomfirmware.h`（asic_init の struct、command list の順、ATOM_INIT・DISABLE、COMBOPHY_PLL0）、`include/atombios.h`（S3 の hung、S6 の ACC_MODE）、`amdgpu/amdgpu_atombios.c`（scratch_regs_engine_hung）、`display/dc/hwss/dcn10/dcn10_hwseq.c`（bios_golden_init）、`display/dc/hwss/dcn20/dcn20_hwseq.c`、`display/dc/optc/dcn20/dcn20_optc.c`、`display/dc/dce/dce_clock_source.c`、`display/dc/bios/command_table2.c`（enable_disp_power_gating_v2_1、set_pixel_clock_v7）、`include/asic_reg/dcn/dcn_2_0_0_offset.h`・`_sh_mask.h`、`drivers/pci/pci.c`（rebar の戻しの順）。どれも GPL/MIT の値と手順の引用で、名前・構造は写していない（audit-symbols PASS）。

## ws.md に写す行（P3 も ws.md を書くので Q1 が写す）

- Phase 一覧の段落に: `[p016](phase016/phase.md) reset（in-progress、q1237 P7: mode1 reset・asic_init（runner）・A2〜A5s の設定し直し・display の書き戻しと A11 の re-light、amdrdnactl reset、自動の reset は amdrdna.reset=1 の時だけ。host 試験と build まで、9d8c0b871、実機は依頼待ち）`。
