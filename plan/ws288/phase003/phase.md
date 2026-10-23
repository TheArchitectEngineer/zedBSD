<!-- awesome-plan project=zedbsd record=ws288-p003 -->

# WS288 p003 CP v0 + BLT + ZYBO の top: 板で `fpgareg` だけの最初の端から端

- 親: [ws.md](../ws.md)。設計: [design.md](../design.md) §8 の p003 の行（§1.3、§2.2〜§2.7 の host interface、§3.6）。前の Phase: [phase002](../phase002/phase.md)。
- 状態: in-progress（2026-10-23 P3。clear の判定は Q1）。受け入れの板の確認は全部 PASS（下の「板の証拠」）。
- 範囲（Q1 の指示、2026-10-23）: design v3（command = Kei GPU command protocol、ISA = RISC-V 基）に依らない物だけ。CP v0 は KICK と DBG だけ（HCR、CP の状態、IRQ、STATE、KICK、非同期の DBG、EXTCAP）。ring と command は p004（v3 の後）。design.md の §2.4・§3 は触らない（P1 が書いている）。
- 受け入れ（design §8）: 板で driver 無しに `fpgareg` だけで fill/copy を DBG で照合、`IRQ_SET` → `fpgareg wait`。加えて p002 の残り: zedBSD の ps7_init が HP0 を 64 bit の mode にしているか、level shifter。

## 置き場所と commit

- amaterrace-gpu（`/home/awe/amaterrace-gpu`、main、push していない）: a2af156（CP v0・BLT・TB）、c99b6dd（`docs/ghci.md`）、704e410（smear の試験）、a6e6d16（BLT を待ちの単純な形に戻し、測った理由を注記）、0c16474（ZYBO の top の注記: 板で確かめた PS の値）。**注意**: 間の c2db6a0・4286a56 はユーザーの README の commit で、その時の作業中の私の差分（書き込みの line の表を入れた BLT、`axi_mem` の `READ_EARLY`）も入った。表は a6e6d16 で外した。9482645・4adf4ba はユーザーの README だけ。HEAD の内容は下の確認の通り。
- zedBSD（worktree `/home/awe/zedBSD-worktrees/p3-ws288-p003`、branch `agent/p3-ws288-p003`）: この phase.md、ws.md の p003 の行、`plan/ws288/tests/zybo-gpu-sd.dts`、`plan/tools/zybo/ghci-blt-board.py`・`ps-regs.sh`、`config/ci/config-zybo.mk`・`platform/armv7/userland.mk`・`plan/tools/zybo/config-zybo-fpga.mk`。

## 作った物（amaterrace-gpu）

| path | 中身 |
| --- | --- |
| `rtl/gpu/cp/atg_cp.v` | CP v0（DEVICE_VERSION 0.2）: p002 の物に EXTCAP（0x100 に PERF の entry 1 つ: PERF0 = BLT の busy の cycle、PERF1 = memory の outstanding の cycle、PERF_SRC0..3 が 0x110）、`PERF_CTRL[0]` の clear、CAPS[11] = 1（4×4 tile）、KICK.DRAW・DISPATCH（無い unit）は FAULT 種別 2。状態遷移（IDLE・RUNNING・HALTING・HALTED・FAULTED・RESETTING）、IRQ、STATE、KICK.BLIT、非同期の DBG、RESET の順は p002 の物のまま。ring は無い（CAPS[0] = 0、RUN は RUNNING にするだけ、STEP は何もしない） |
| `rtl/gpu/blt/atg_blt.v` | 書き直し: 16 bit の単位で歩く。FILL・COPY・**TILE・UNTILE**（Amaterrace の 4×4 tile: tile の中は行順、tile は左から、stride は tile の 1 行の byte）、**16 bpp**、**blend-over**（32 bpp の FILL と COPY、premultiplied alpha の source-over、byte ごとに round(dst × (255 − a) / 255) を足して飽和。3 段の pipeline で 1 pixel/cycle）、**逆向き**（FILL と COPY: 下の行から右から左、重なる copy で dst が src の後の時）、clip。対応しない組み合わせ（bpp 2・3、16 bpp か tile の blend、tile の逆向き）は FAULT 種別 2。segment は dst の 1 line（tile の時は tile の 1 行）に収め、address を 4 cycle で計算 |
| `include/ghci_regs.vh`・`.h` | `GHCI_EXTCAP_LIST`・`GHCI_PERF_SRC0`・PERF_SRC の値、BLT_OP の bit |
| `sim/tb/atg_top_tb.v` | 参照の model を書き直し（16 bit の pixel、tile の address、blend、「src を全部読んでから書く」= 正しい向きの重なりの結果）。直接の試験: 16 bpp の FILL・COPY、blend の FILL・COPY、TILE → UNTILE、tile 16 bpp、重なる copy（逆向き、前向き、16 bpp で 1 pixel 右）、逆向きの clip 付き FILL、**smear**（向きの誤った重なり: segment ごとに前の segment の書き込みを読む、の model と一致 = 読みが先行の書き込みを待つ事の試験）、EXTCAP・PERF、bpp 2・tile の blend・KICK.DRAW の FAULT 種別 2。random 120 回（全種、tile は tile 揃え）。`+seed` を受ける |
| `sim/models/axi_mem.v` | `READ_EARLY`: 読みの data を AR の受理の時に取る（読みが書きを追い越す memory の model）、その時は WREADY を 20% に。atg_top_tb で使う。読みが書きの完了を待たない BLT の改変を smear の試験が落とす事を確かめた（mutation） |
| `docs/ghci.md` | PERF capability の register、BLT の各 option の意味・4×4 tile の配置・重なりの向き・FAULT 種別 2、KICK の無い unit、Amaterrace の実装の現状（p003） |
| `boards/zybo/atg_zybo_2d.v` | 注記: 板で確かめた PS の値（下） |

## 確認（host）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| lint | `tools/lint.sh`（Verilator 5.032 `-Wall`） | 11 件すべて clean（warning 0） |
| unit TB | `sim/run.sh`（iverilog 12.0、warning も FAIL） | PASS am_afifo、am_axi_master、am_axi_regs、atg_mc、atg_top。`SEED=2,3,42` でも atg_top PASS、`SEED=3,42` で am_axi_master・atg_mc PASS |
| Verilator の harness | `sim/verilator/run.sh` | PASS verilator（6120 cycle、build の warning 0） |

TB の途中で見つけて直した物: blend の積が 8 bit に切れていた（Verilog の連結の中の積は自分の幅。lint の WIDTHEXPAND で見つけた。16 bit の operand の関数にした）。TB 自身の誤り（TILE の src と dst の重なり、tile 16 の stride 不足）。

## bitstream 1 の合成の実測（Vivado 2026.1、BASIC、xc7z010clg400-1、配線後、amaterrace-gpu 0c16474）

`tools/vivado-build.sh zybo atg_zybo_2d`（162 s）。

| 項目 | p003 | p002 |
| --- | --- | --- |
| Slice LUT | **3,802**（21.6%）: regs 289、CP 610、BLT **2,727**、MC 173（AXI master 156） | 2,451（BLT 1,392、CP 601） |
| Slice Register | 3,719（10.6%）: BLT 2,196、MC 853、CP 531、regs 111 | 2,632 |
| Block RAM | 0.5（STATE の BRAM18） | 0.5 |
| DSP | 0（blend の 8×8 の積 4 つは LUT） | 0 |
| timing（100 MHz） | WNS **+0.190 ns**、WHS +0.048 ns。最悪の道は BLT の `q_valid` → MC の arbiter → AXI master の `w_buf` の CE（256 bit の fan-out、配線 75%） | WNS +0.473 ns |
| warning | 130（PS7 の未接続 100・接続数 1、意図して使わない bit 24、定数の LED 4、POWER 1）。critical 0 | 132 |
| bitstream | 2,083,942 byte、sha256 先頭 65a5b91f27a6c0fb（commit しない） | |

design §3.6 の比較: BLT は p002 で直した 1.8〜2.2k の見込みに対して 2.73k（4×4 tile と逆向きの address の計算、16 bit の単位の shifter、blend の LUT の積と line の buffer 3 本）。CP は ring 無しで 0.61k のまま（1.8k の見込みは ring・fence の p004 で測る）。bitstream 1 の全体は 3.8k（21.6%）。

試して外した物: 読みが先行の書き込みの全部を待つ代わりに、書き込み中の line（8 個の表）と同じ line の読みだけを待つ形（c2db6a0）。板で COPY が 49.7 → 40.3 cycle/line（19% 速い）、BLT は +0.75k LUT（3,482）・WNS +0.428 ns。COPY は 1 segment ずつの読みの latency で決まるので費用に見合わず、a6e6d16 で戻した（RTL の注記に数字）。

## 板の証拠（実機の ZYBO、SD から単独起動、zedBSD 1.0.0-beta2+g5a28d28、10.0.30.2。QEMU は使っていない）

板の変更（どれも戻せる、`.old` を残した）:

- `/sbin/fpgaload`・`/sbin/fpgareg` を scp（この worktree で build、`make ZEDBSD_CONFIG=plan/tools/zybo/config-zybo-fpga.mk BUILD=build/ws288-p003 build/ws288-p003/bin/fpgaload build/ws288-p003/bin/fpgareg`、warning 0、各 11 KiB）。SD の今の root には入っていない（下の zedBSD の変更で次の card から入る）。
- `/boot/boot0/system.dtb` を `plan/ws288/tests/zybo-gpu-sd.dts`（`zybo-sd.dts` + `/reserved-memory/gpu@1c000000` 64 MiB no-map）に。`make-zybo-boot-files.py dtb` で作り、deploy.sh と同じ手順（`.new` に書き cksum を照合して rename）で置き reboot。前の物は `system.dtb.old`。dmesg の reserved が 6,972 KiB → **72,508 KiB**（+65,536）。design は「memory@0 を 448 MiB に」だが、`make-zybo-boot-files.py` が memory@0 = 512 MiB を検査するので、kernel が既に読む `/reserved-memory`（`src/hal/armv7/fdt.c`・`page.c`）にした。

p002 の残り（`plan/tools/zybo/ps-regs.sh`: JTAG の AHB-AP で zedBSD の動作中に読む。SD の boot mode でも JTAG は使えた）:

| register | 値 | 意味 |
| --- | --- | --- |
| AFI0 RDCHAN_CTRL 0xF8008000 | 0x00000000 | bit 0 32BIT_EN = 0 → **HP0 の読みは 64 bit** |
| AFI0 WRCHAN_CTRL 0xF8008014 | 0x00000F00 | bit 0 = 0 → **書きも 64 bit**（reset の値。U-Boot SPL の zybo の `ps7_init_gpl.c` は AFI を書かない） |
| AFI1 0xF8009000・0xF8009014 | 0、0xF00 | 同じ |
| LVL_SHFTR_EN 0xF8000900 | 0x0000000F | **level shifter は両向き有効**（devcfg の `FPGA_ENABLE`、`fpgaload` の後） |
| FPGA_RST_CTRL 0xF8000240 | 0 | PL の reset は解除 |
| FPGA0_CLK_CTRL 0xF8000170 | 0x00100A00 | IO PLL / 10 = **100 MHz** |

HP0 の 64 bit は機能でも確かめた: DBG で 64 bit の beat の上下（0x1C000000 と 0x1C000004）を書いて読み戻し一致。

板の受け入れ: `plan/tools/zybo/ghci-blt-board.py`（host から SSH で `fpgaload` と 1 本の shell script を流し、DBG で各 blit の窓を前後に読み、host の model（TB と同じ）と比べる。seed で pattern と色を変える。約 2 分）。最終の bitstream（65a5b91f27a6c0fb）で 5 回、全部 PASS: `pl-jtag.sh`（JTAG）で書いた PL で 1 回、`--bit`（fpgaload）で 1 回（この 2 回は seed を足す前で、窓に前の回の結果が残り変化した word が少なかった。seed はそのために足した）、その fpgaload の PL のまま seed 1792753777 で 1 回と seed 7 で 2 回。表は seed 1792753777 の回:

| check | 結果（seed 1792753777） |
| --- | --- |
| identification、DEVICE_VERSION 0.2、CAPS 0x00501802、EXTCAP（0x100 → PERF、0x110: src 1 と 8）、aperture | PASS |
| DBG の書き込み 192 word と読み戻し、DBG_STATUS 0 | PASS |
| fill32（13×3、変わった 39 word）、copy32（15×4 不揃い、60）、fill16（21×3、33）、copy16（25×3、39）、fill-blend（9×2、18）、copy-blend（10×3、30）、tile（8×8、64）、untile（8×8、64）、overlap-reverse（12×4、48）、overlap-forward16（20×3、33）、RESET の後の fill（12） | すべて窓の全 word が model と一致 |
| PERF0・PERF1 が数え、PERF_CTRL で 0 | PASS |
| `IRQ_SET` → `fpgareg wait --ack 0x38` = `irq count 1 status 0x00000020`、2 回目の wait は `Connection timed out`（W1C で下がった） | PASS |
| HALT → 背景の `fpgareg wait` = `status 0x00000040`、STATUS 0x00035d00（HALTED） | PASS |
| aperture の外の FILL → `fpgareg wait` = `status 0x00000002`、STATUS 0x00045b00（FAULTED）、FAULT_INFO 0x00011101（種別 1、client 1、write）、FAULT_ADDR = base − 0x100 → RESET → IDLE、FAULT_INFO 0 → 次の fill が通る | PASS |

PL の書き込みの道: `fpgaload /tmp/….bit` は 0.21 s（zedBSD の devcfg の DMA）。`plan/tools/zybo/pl-jtag.sh` は **SD の boot mode でも動いた**（openFPGALoader 3 s、その後 `fpgaload --enable` で window が開き、GHCI が読めた）。

性能（板、1024×256 = 1 MiB、PERF0 = BLT の busy の cycle、100 MHz）:

| 操作 | cycle | 時間 | MB/s | cycle/line（32 byte） |
| --- | --- | --- | --- | --- |
| FILL 32 bpp / 16 bpp | 163,864 | 1.64 ms | 640 | 5.0（address の 4 cycle + 書き 1） |
| COPY 32 bpp（揃い）/ 逆向き / 16 bpp | 1,628,691 | 16.3 ms | 64 | 49.7 |
| COPY 32 bpp（src 1 pixel ずれ: 2 line の読み） | 1,760,324 | 17.6 ms | 60 | 53.7 |
| FILL + blend | 1,929,072 | 19.3 ms | 54 | 58.9 |
| COPY + blend | 2,955,814 | 29.6 ms | 35 | 90.2 |
| TILE 32 bpp（segment は 4 pixel） | 3,165,985 | 31.7 ms | 33 | 96.6 |

COPY は segment ごとに「先行の書き込みの完了を待つ → 読む → 書く」を直列にするので、HP0 の読みの latency と書きの応答の往復で 1 line 約 50 cycle。速くするには読みを複数 segment 先まで出す（prefetch、request の FIFO と data の道を分ける）書き直しが要る（残り）。

## zedBSD の側（この worktree）

- `config/ci/config-zybo.mk` の `ZEDBSD_USER_PROGRAMS` に `fpgaload fpgareg`、`platform/armv7/userland.mk` で armv7 の root が `ZEDBSD_USER_PROGRAMS` の中の `fpgaload`・`fpgareg` を入れる（`ARMV7_ROOT_PL_COMMANDS`）。理由: SD で起動する既定の ZYBO の root で、GPU（PL）の試験に毎回 scp しなくてよい。build の menu（`tools/menuconfig.py`）は `ZEDBSD_USER_PROGRAMS` だけを書くので、menu で保存しても消えない形にした（`ARMV7_ROOT_EXTRA_COMMANDS` は menu が書かない）。大きさは 2 つで 22 KiB（SD の root は 256 MiB、JTAG の ramdisk の root 16 MiB の中）。`plan/tools/zybo/config-zybo-fpga.mk` の `ARMV7_ROOT_EXTRA_COMMANDS := fpgaload fpgareg` は二重になるので外した（`make -n` で両方の config で `--file /sbin/fpgaload`・`/sbin/fpgareg` が 1 つずつ）。**image は作っていない**（実装の担当は disk-image を流さない）。新しい SD の card に入るのは Q1・T1 が image を作る時。
- `plan/ws288/tests/zybo-gpu-sd.dts`: GPU の 64 MiB を予約した SD 起動の DTB（上）。
- `plan/tools/zybo/ghci-blt-board.py`: 板の受け入れの道具（p004・p006 でも使える。Master の Tools への登録は Q1）。
- `plan/tools/zybo/ps-regs.sh`: 動作中の板の PS の register を JTAG で読む（登録は Q1）。

## 未実施・制限・残り

- QEMU は対象外（ZYBO の PL は QEMU に無い）。証拠は全部実機。
- 板の SD の root に fpgaload・fpgareg が入るのは次の image から（今の板は scp の物）。
- 板の system.dtb は GPU の予約付きのまま（`.old` が元）。p004 以降もこのまま使う想定。戻すなら `system.dtb.old` を rename して reboot。
- COPY・blend・tile の性能（上の表）。読みの prefetch は p004 以降の残り（Q1 の判断）。
- design の p003 の行の「DDR の領域は DTB の memory@0 を 448 MiB に」は `/reserved-memory` に代えた（上）。design.md の §8 の文の直しは Q1・P1（§2.4・§3 は P1 の版 3 で触らない約束）。
- この worktree の `build/llvm-source`（2.7 GB）は最初の make が compiler-rt の builtins のために worktree の中に展開した物（共有の tree ではない）。消すのは Q1。
- 試験の整理: TB は amaterrace-gpu の回帰。`ghci-blt-board.py` は板の回帰として Master の Tools に載せる候補。
