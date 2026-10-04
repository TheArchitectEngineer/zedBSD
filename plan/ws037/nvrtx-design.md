<!-- awesome-plan project=zedbsd record=ws037-design -->
# nvrtx の設計（ws037-p001）

作成: 2026-10-04、P2（q692）。状態: 設計の案（code は無い）。design-reviewer の review（2026-10-04 の 2 回分: 高 4・中 11・低 12 と、重大 4・中 10・軽 4。重なりを除き全部）を反映した版。

この文書は GPL の code・comment・定数の名前を含まない。手順は事実だけを自分の言葉で書く。class の番号・SM の番号・RPC の番号・register の offset は事実の数値として、段の設計に要る最小限だけを書く（[Guardrail](../guardrail.md) の WS141・WS037 の節）。値の一覧と手順の詳細は commit しない作業の文書にある。正本の版・hash・license は [nvrtx-license-audit.md](nvrtx-license-audit.md)。

| 作業の文書（`plan/ws037/temp/`、commit しない） | 中身 |
| --- | --- |
| `gsp-boot.md` | PCI の発見・chip の判定・BAR・reset、devinit の待ち、VBIOS と FWSEC、WPR2、GSP の firmware の section・page table・args・queue、booter、RISC-V の起動、RPC の形と番号、RM の object、停止・suspend・resume、firmware の file、各段の確かめる値（段の名前は古い案で、この文書の 3 節が正） |
| `channel-mmu-display.md` | Turing の MMU の page table、vaspace と BAR1・BAR2、channel の作り方と doorbell、push buffer・GPFIFO・fence、class と golden context、RC と回復、NVDisplay の class・初期化・GOP の readout・mode set・flip・vblank・EDID と link、NVK が kernel に求める物 |
| `mesa-generations.md` | NVK・NAK の世代の差（class・記述子・shader header・encoder・latency の表・Vulkan の機能）、最初の世代を選ぶ材料 |

どの文書も source を読んだだけで、**実機・QEMU での観測は無い**。

## 1. 最初の対象と構成

### 1.1 最初の対象: TU106（RTX 2070、Turing、SM75）

- chip の判定は PCI の device ID ではなく、BAR0 の先頭の register（boot0）の bit 28:20 が 0x166 であることで行う（nouveau の device ID の表は製品の名前の表で、Turing の chip の判定には使われない）。device ID は表示にだけ使う。
- NVIDIA の open-gpu-kernel-modules の README の表で、RTX 2070 は 0x1F02・0x1F07（desktop）。0x1F00〜0x1F7F が TU106 と推定（README に chip の名前は無い）。
- **RTX 2070 SUPER は TU104**（0x1E84・0x1EC2・0x1EC7、推定）で TU106 ではない。nouveau では同じ GSP の手順と同じ firmware（tu104 も tu102 の物を指す）なので、TU104 でも手順は同じ見込み。実機がどちらかは p002 で確かめる（判断の項目 1）。
- boot1 の bit 17:16 が立っていれば vGPU（VF）なので、**ENODEV で attach しない**。

### 1.2 構成

| 部品 | 役割 | 要点 |
| --- | --- | --- |
| BAR0 | register の窓 | 先頭の register で chip の判定。PCI の config space の写しが 0x088000〜にある |
| BAR1 | VRAM の CPU の窓（約 256 MiB の見込み、大きさは N0 で読む） | GSP の下では BAR1 の page table の**根**（最上段）を GSP-RM が持ち、**下の段は kernel が書く**（M1）。窓を使い切ったときの方針は 8 節の `resource_map` |
| BAR2（PCI の BAR の番号は固定でない: BAR0・BAR1 が 64 bit かで決まる。N0 で数える） | instance memory の窓 | kernel が page table を作り、根を RPC で GSP-RM に知らせる。前半だけを kernel が使う |
| GSP（falcon と RISC-V） | GSP-RM（NVIDIA の resource manager）を走らせる | firmware は file から load。kernel と共有 memory の queue（cmd・msg）で RPC |
| SEC2（falcon） | 署名付きの booter（起動・停止の ucode）を走らせる | booter が GSP-RM の image を WPR2 に置いて GSP を起こす（booter の中で何が起きるかは正本に無く、**推論**） |
| VBIOS | FWSEC（署名付き ucode）を含む | FWSEC-FRTS が VRAM の末尾に WPR2 を張る。停止では FWSEC-SB |
| VRAM の末尾 | WPR2（GSP-RM の image・heap）と VGA の workspace | 8 GiB の例で WPR2 約 135 MiB、heap 約 105 MiB（式は作業の文書） |
| DMA | GPU が host の memory を読む | 47 bit の address（**sysmem の flush page だけは 40 bit 以下**）。GSP に渡す番地は全部 bus（DMA）番地で、GSP-RM が動く間ずっと有効でなければならない。zedBSD の DMA の vector は 64 KiB までなので、28.5 MB の image は 64 KiB の vector の集まり（か散らばった page）にして GSP の 3 段の page table で束ねる（連続の大きな確保に頼らない）。HAL と generic DMA の変更は要らない見込み（p004 で判定） |

### 1.3 何を GSP-RM がし、何を kernel がするか（Turing・570.144 の GSP の道）

| GSP-RM（RPC で頼む） | kernel（zedBSD）が自分でする |
| --- | --- |
| object の木（client・device・subdevice・vaspace・channel・engine の object）、channel の schedule・preempt・停止、GR の golden context、RC（channel の error）の検出、display の SOR の割り当て・DP の link training と AUX・EDID・HDMI・supervisor の処理、BAR1 の page table の根 | GPU の仮想 address の page table（5 段、4K・64K・2M の page）と TLB の invalidate、BAR1 の page table の下の段、BAR2 の page table、instance block・USERD・GPFIFO・push buffer の memory、doorbell、fence の semaphore と non-stall の割り込み、display の core・window channel の push buffer（mode・surface・flip）、vblank の割り込み、CPU sequencer の要求（RM が kernel に register の操作を頼む）の実行 |

割り込みの木: GSP の msg queue の到着は GSP の falcon の software の割り込み（作業の文書）、channel の non-stall・stall、display、fault の各 vector。どの vector を kernel が受け持つかは P3 で RM に聞いて決める（割り込みの表の RPC）。

## 2. 方針の要点

0. **scanout の規則（2026-10-04 ユーザー「GPUドライバはGOPの出力先以外に、scanoutを開始しない.というルールを覚えておいてください。」、Guardrail）**: nvrtx は UEFI の GOP の出力先（GOP が点けた head と connector）以外に scanout を始めない。P8 の自前の surface（判断の項目 7）は同じ出力先に限る。別の connector への出力は compositor の明示の指示がある時だけ。
1. **GSP-RM の道だけを作る**（判断の項目 3）。Turing は GSP 無しでも nouveau で動くが、GSP 無しの道は clock の管理（reclocking）を持たない見込み（推論、未確認）なので、GSP の道を取る。nouveau で GSP が必須なのは GA100・Ada・Hopper・Blackwell（GA10x と Turing は任意、7 節の表）。**Ampere・Ada は同じ booter 型の起動**（SEC2 の booter と VBIOS の FWSEC）なので Turing の道の延長になるが、**Blackwell（と範囲の外の Hopper）は別の起動の仕組み**（FMC 型の firmware）で、Turing の P1・P2 の段を作り直す（7 節）。
2. **firmware の版は 570.144 に固定**（判断の項目 4）。RPC の struct は版ごとに変わるので、版を混ぜない。struct の定義は open-gpu-kernel-modules 570.144（MIT）から取る。
3. **WPR2 を張る前に、失敗しうる準備を全部済ませる**（H4）。P1c（FWSEC-FRTS）の前に、firmware の file の読み込みと検査（大きさ・header・ELF の section、`gsp-570.144.bin` は約 28.5 MB）、GSP に渡す DMA の memory の確保（firmware の page・page table・args・log・queue）、FWSEC-SB の ucode の取り出し、WPR2 の配置の計算を終える。準備のどれかが失敗したら hardware に書かずに止まる。firmware は `/lib/firmware` から読むので、**GSP の起動は root の mount の後に非同期**で行う（attach の時は N0・N1 だけ）。
   nouveau の順（準備は全部 FRTS の前）: ELF・署名・3 段の page table の用意、FWSEC-SB の image の用意、libos の初期化、system info と registry の RPC を cmd queue に積む → WPR の layout と meta → FWSEC-FRTS → GSP を RISC-V の起動に切り替え libos の args の番地を GSP の mailbox に書く → booter_load → app の版を書く。FWSEC-SB は起動の時に用意して停止まで保つ（停止の時に VBIOS を読み直す前提にしない）。firmware の読み込みは i915 の口（root の mount を待ち、1 MiB まで）では足りないので、上限の大きい読み込みを driver に持つ（p004）。
4. **段ごとに失敗の後に残る物を決め、自動の再試行はしない**（3.1 の表）。load の前に WPR2 の register を読み、残っていれば止まる（nouveau は読まない）。WPR2 が張られた後の失敗では、**FWSEC-SB と booter_unload を 1 回だけ試し**、WPR2 の上限が 0 に戻らなければ device を「電源の切断が要る」と記録して fault のまま止まる（判断の項目 14）。GSP-RM が起きる前に booter_unload が WPR2 を解けるかは正本に根拠が無い（未確認）。zedBSD の reboot・shutdown では P7 を走らせる。panic では走らせられないので、次の起動で WPR2 を確かめて止まる。
5. **IOMMU・DMA の寿命**: GSP に渡す memory（firmware の page、page table、args、log、queue、FWSEC の ucode）は、GSP-RM が動く間ずっと map と物理を保つ。driver の終了と device の fault でも、GSP を止めるまで返さない（`drv_gpu_recovery_ops` の fault の契約と同じ）。
6. **RPC は非同期**（M3）。zedBSD の `stop_begin`・`stop_poll` は待てず、一つの session の ioctl 以外は並行に来る。RPC は一つの worker（RPC の担当）が cmd queue への投入と msg queue の受信を持ち、呼び手は要求を積んで完了を待つ（待てる文脈だけ）か、poll で状態を見る。同じ function の RPC は直列、応答は queue の sequence で照合する（function の番号だけで照合しない）。RPC の timeout と GSP-RM の異常（log の停止、RISC-V の停止、error の event）は device の fault（`drv_gpu_report_error`）。
7. **GOP の画面について 2 つの危険を分ける**（H1・低）: (a) 画面が消える（scanout が止まる）、(b) CPU の GOP の framebuffer の窓が使えなくなる（BAR1 が GSP-RM の page table に切り替わり、GOP の framebuffer の物理の窓が別の物を指す）。(b) では text console が書くと VRAM の別の物を壊しうる。段の印の出し先は 5 節。

## 3. 段の一覧

i915 の名付けに合わせる（N = GPU の状態を変えない、P = 変える）。**この表が段の名前の正**（ws.md の表もこれに合わせた。作業の文書 `gsp-boot.md` の P4 = 停止は古い案）。

| 段 | すること（自分の言葉で） | 確かめる値 | Phase |
| --- | --- | --- | --- |
| N0 | PCI の発見、BAR の大きさと種類、BAR0 の map、boot0・boot1 で chip の判定、PCI の FLR の能力の bit を読む | boot0 の chip = 0x166（TU104 なら 0x164）、boot1 の vGPU の bit が 0（立てば ENODEV）、BAR0 の読みが全 1 でない、BAR の大きさ（未知、記録する） | p003 |
| N1 | GOP の画面の readout（読むだけ）: loader が渡す GOP の情報（幅・高さ・stride・物理）を記録。display の register の ARMED の状態（timing・surface）を読めるか試す。**方式 A（素の起動）だけ**（方式 B では GOP の画面は emulated な display の物） | GOP の framebuffer の物理が nvrtx の BAR1 の窓の中にあるか。読めた timing が GOP の大きさと合うか | p003 |
| P0 | GSP の起動の準備（hardware に書かない、root の mount の後）: firmware の読み込みと検査、DMA の確保、VBIOS の読み（PRAMIN を先に、だめなら PROM）、FWSEC-FRTS と FWSEC-SB の取り出し、WPR2 の配置の計算、text console の切り離し（5 節） | 全部の準備が成功。VBIOS の image の署名、FWSEC の記述子の版（v2 の見込み、未確認） | p004 |
| P1a | devinit の完了待ち: VBIOS の script は実行せず、firmware（GFW）の devinit が終わった印を待つ | 完了の bit と進み具合の値が「完了」（数秒以内） | p004 |
| P1b | WPR2 が張られていないことを確かめ、FWSEC-FRTS を GSP の falcon で走らせる（`acr/bl.bin` を bootloader に） | falcon の停止、scratch の error code が 0、WPR2 の下限・上限が 0 でなくなる | p004 |
| P2a | （system info と registry の RPC は P0 で FRTS より前に積んである）GSP を RISC-V の起動に切り替えて libos の args の番地を GSP の mailbox に書き、text console を切り離し（5 節）、booter_load を SEC2 で走らせ、app の版を書く | SEC2 の mailbox が 0、falcon の停止 | p004 |
| P2b | GSP の RISC-V の起動を確かめ、初期化の完了の event を待つ。msg queue は GSP の割り込みで常に処理し、CPU sequencer の要求は起動の時から受ける | RISC-V の状態の bit、初期化の完了の event（0x1001）が数秒以内、log の buffer の put | p004 |
| P3 | static info の RPC、RM の object（client → device → subdevice）、割り込みの表（kernel 用の vector）、BAR2 の根の通知、BAR1 の下の段 | 各 RPC の status が 0、static info の長さ | p004 |
| P4 | VRAM の allocator（WPR2・VGA の workspace・GOP の framebuffer を除いた範囲）、MMU（自前の page table を作り外部所有の vaspace として RM に登録）、BAR1・BAR2 の map の口 | page table の読み返し、TLB の invalidate の完了 | p005 |
| P5 | GR の golden context（RM が作る）と TU10x の scrubber の channel（570.144 の RM の既知の回避）、channel（instance block・USERD・GPFIFO・method buffer を用意して RM に確保させ、bind と schedule）、doorbell、fence（semaphore の release と non-stall の割り込み） | 何もしない push の後に semaphore の値が進む、non-stall の割り込みが来る | p005 |
| P6 | 回復: RC の event、channel の停止と preempt の確認（RM の ctrl）、`isolate`・fault・reset（8 節） | RC の event の受け取り、preempt の確認、channel の再作成 | p005 |
| P7 | 停止: unload の RPC → GSP の mailbox の完了の値（0x80000000）→ GSP の falcon の reset → FWSEC-SB → booter_unload。reboot・shutdown でも流す | WPR2 の上限が 0 に戻る | p004 |
| P8 | display: RM の display の object・core channel・window channel を作り、GOP の画面の head・SOR を引き継ぎ（RM に聞く）、自前の surface（VRAM、P4 の allocator と BAR1 の map）を window に出す。flip は window channel の offset の更新と update、完了は notifier、vblank は head の timing の割り込み。text console を自前の surface に付け直す | 画面に自前の surface、notifier の完了、vblank の回数が refresh に合う | p006 |
| P9 | 最初の 3D・compute・copy の job: 各 subchannel に class の object を bind し、GR の context を RM に渡して 3D の object を作り、copy の fill と compute の空の dispatch を流す | semaphore の進み、copy の結果 | p007 |

- suspend・resume は範囲の外（後の Phase）。Turing の suspend には 50 ms の待ちの回避がある（作業の文書）。
- system info の「primary の GPU か」の欄（RM が GOP の画面を保つかに関わる可能性、推測）の値は判断の項目 18。
- reset と display: reset（P7 と再起動）は GOP の mode を残さないので display も落ちる。display を claim した session には device の喪失を publish し、reset の後に P8 を作り直す（判断の項目 17）。

### 3.1 段ごとの失敗の後に残る物と回復（自動の再試行はしない）

| 失敗した段 | 残る物 | 回復 |
| --- | --- | --- |
| N0・N1・P0 | 何も（hardware に書いていない） | attach しない（P0 の失敗は GSP を起動しないだけで、N1 の GOP の console は続く） |
| P1a | 何も | 止まる。GFW の devinit が終わらない device は使わない |
| P1b（FRTS の後の検査の失敗） | WPR2（張られていれば） | FWSEC-SB と booter_unload を 1 回だけ試し、上限が 0 に戻ったか確かめて止まる。戻らなければ「電源の切断が要る」と記録して fault（判断の項目 14）。FRTS の後に失敗しうる確保は残さない（2 節の 3） |
| P2a・P2b | WPR2、GSP の image、DMA の memory | P7 を全部流してから DMA の memory を返す。P7 が完了しなければ DMA の memory を返さず fault |
| P3〜P9 | GSP-RM が動いている | その段の object を RM に消させ、必要なら P7。GSP-RM の異常なら fault（DMA の memory は保持） |

## 4. command の投入の順（自分の言葉で）

1. push buffer に method を書く（header は「連続の register に増えながら」「同じ register に繰り返し」「header の中の値だけ」「最初の 1 つだけ増える」の 4 種）。
2. GPFIFO の ring に entry（8 byte、push buffer の GPU の仮想 address と長さ）を書く。
3. USERD の GP_PUT を進める → BAR1 の書き込みを読み戻しで確定 → doorbell の register に channel の token を書く。token は RM に聞いて得る（nouveau は自分で組み立てているが、RM の ctrl で取る方が安全、未確認）。
4. 完了: push の最後に host の class の method で semaphore の release（値と address、wait-for-idle 付き）→ system の memory barrier → non-stall の割り込み。kernel は割り込みで semaphore の値を読み、fence を完了させる。**fence の値だけでは buffer の解放を決めない**（8 節の安全の模型）。
5. 3D・compute・copy の class は subchannel ごとに最初に object を bind する。GR の context は RM が golden context を作り、kernel が context の buffer を用意して RM に渡す。

display の flip: window channel の push buffer に surface の offset（VRAM の物理の範囲の context と offset）・大きさ・形式を書き、update を送り、push buffer の PUT を進める。完了は notifier の memory の状態を見る。

## 5. 段の印の設計

- **出し方**: i915 と同じく driver が段の始めと終わりに `kern_logf` で行を出す（例 `nvrtx: N0 begin`、`nvrtx: N0 ok chip=166`、`nvrtx: P2b ok`）。amd64 では console の文字が GOP の framebuffer の text console（`src/drivers/platform/pcat/graphics/text.c`）に出る。1 行は console の桁に収める。`kmsg=quiet` では `kern_logf` が画面に出ないので、試験の image は `kmsg=console` にする（logo を無効にした表示の build、Guardrail の「表示の build」）。
- **危ない段の前で待つ**: P1b（FWSEC-FRTS）・P2a（booter）・P8（display）の前に begin の行を出す。数秒の待ちは debug の parameter（例 `nvrtx.pause=1`）がある時だけにし、既定の boot を遅くしない。
- **段で止める boot の parameter**: 例 `nvrtx.stop=P1b`、`nvrtx.off=1`。1 段ずつ進める。kernel の既知の parameter の表（`src/kern/boot.c`）は変えず、WS141 と同じく生の command line を `kern_boot_parameters_token_present` で語ごとに照らす（未知の parameter は起動の log に 1 行出るだけ）。
- **text console の切り離し**（H1）: 今の amd64 の text console の suspend は flag を立てるだけで、framebuffer の surface は付いたままで、致命的な error の表示（reveal_fatal）・splash の ticker・`/dev/graphics` の resume（所有者の照合なし）が GOP の framebuffer の窓に書きうる（`src/drivers/platform/pcat/graphics/text.c`・`pcat-graphics.c`）。i915 は text console の口を呼ばないので手本が無い。nvrtx は **P2a の begin の行を出した後、booter_load の直前**に（BAR1 の持ち主が変わる直前）、**GOP の framebuffer が nvrtx の BAR1 の中にある時だけ**、text・splash・panic の表示・`/dev/graphics` の framebuffer に書く経路を全部止め（surface を外す）、P8 で自前の surface に付け直す（保持した文字の格子が描き直されるので、成功すれば P2〜P7 の印も画面に出る）。GOP の framebuffer の VRAM の範囲は P8 まで kernel の VRAM の allocator から除く。代わりの案: P3 の後に GOP の VRAM を同じ BAR1 の offset へ kernel が map し直す（BAR1 の下の段は kernel が書くので可能な見込み、未確認）。この「surface を外す・付け替える」口は pcat の text（`src/drivers/platform/pcat/`、WS037 の範囲の外）に要る新しい口で、**計画に無い依存**（判断の項目 12）。
- **GSP の起動の後**: P8 で display を引き継ぐまで、方式 A では画面に印が出ない可能性がある（P2〜P7 の印は kernel の log にだけ残る）。方式 B では guest の emulated な display を主の console にし、そちらに印を出して QMP の screendump で撮る（6 節）。PRAMIN の窓で GOP の framebuffer に印を描く案は、RM と窓を取り合う危険があるので取らない。方式 A で GSP の起動が止まった時の証拠は、scratch の register に段の番号を残して次の起動の N0 で読む案がある（p004 で決める、判断の項目 6）。

## 6. 試験の道

| 方式 | 中身 | 印の見方 | 注意 |
| --- | --- | --- | --- |
| A: USB から素で起動 | amd64 の image に nvrtx の段の印の kernel を入れて、RTX 2070 の host を USB から起動 | ユーザーの写真か capture の器具（RTX 2070 の出力） | N1 と text console の切り離しを試せるのは A だけ。WPR2 が残ったまま失敗すると、host の電源を切るまで再試行できない見込み |
| B: VFIO | host（ユーザーの予定は開発の host の centris、2026-10-04 WS139 U5。共有の host なので危険を判断の項目 15 で確かめる）の Linux で RTX 2070 の全 function（video・audio・USB・UCSI）を vfio-pci に付け、QEMU の guest に渡す | guest の emulated な display に段の印を出し、`plan/tools/boot-test.sh` と同じ画面の撮り方の道具で撮る（zedBSD の image は boot-test.sh の扱い。AGENTS.md の SSH/QMP PNG の例外は Linux・FreeBSD の guest の物）。RTX の出力は写真 | 下の 4 点 |

方式 B の注意（M9）:

1. **GOP が 2 つ**（emulated な display と passthrough の GPU の option ROM）になる。loader が渡す framebuffer は 1 つだけなので、emulated な方を選ばせる。text console の切り離しは GOP が nvrtx の BAR1 の中の時だけなので B では起きない（撮る console は止まらない）。その代わり N1 と P8 の GOP の引き継ぎは B では試せない（方式 A だけ）。
2. **判定は画面の PNG と serial・SSH の対話**（AGENTS.md）。WS029 p006 の i915 の手順にある guest の log を読んだ判定は今の規則に反するので写さない。
3. **QEMU を強制終了すると WPR2 が残る**: harness は guest の中で停止の手順（P7）を必ず流してから QEMU を止める（`nvrtx` の停止の口を試験から呼ぶ）。残ったら試験を止め、host の電源を切る（bus reset で戻るかは未確認。slot の D3cold と `reset_method` の調べを p002 に入れる、判断の項目 14・15）。
4. 試験は試験の担当（T1・T2）が Q1 経由で流す。host と電源の切り方は未定（判断の項目 15）。host の操作（vfio-pci への付け替え、host の表示の停止）はユーザーの承認の後。VFIO で BAR0 の PCI の config の写しの PROM の読みが QEMU に横取りされうる（推測）ので、VBIOS は PRAMIN の窓を先に試す。

host の情報は [host.md](host.md)（ユーザーの情報待ち）。p002 で決める。

## 7. 世代の差（事実の表）

Mesa 25.3.6 と Linux v6.19 の nouveau の class の表と GSP の firmware の一覧を読んだ結果（2026-10-04）。

| 世代（chip） | SM | 3D | compute | copy | 2D | inline-to-memory | channel（GPFIFO） | compute の記述子 | graphics の shader header | nouveau で GSP が必須か | GSP の firmware（linux-firmware の directory、570.144） | GSP の起動の仕組み |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Turing（TU10x・TU11x） | 75 | 0xC597 | 0xC5C0 | 0xC5B5 | 0x902D | 0xA140 | 0xC46F | 2.2・256 B | 版 4・128 B | 必須でない | `tu102`（tu104・tu106 は link）、TU11x は `tu116` | booter 型（SEC2 の booter、VBIOS の FWSEC） |
| Ampere（GA100） | 80 | 0xC697 | 0xC6C0 | 0xC6B5 | 0x902D | 0xA140 | 0xC56F | 3.0・256 B | 版 4・128 B | 必須 | `ga100` | booter 型 |
| Ampere（GA10x） | 86 | 0xC797 | 0xC7C0 | 0xC7B5 | 0x902D | 0xA140 | 0xC56F | 3.0・256 B | 版 4・128 B | 必須でない | `ga102`（ga103〜ga107 も） | booter 型（FWSEC の記述子の版が違う見込み） |
| Ada（AD10x） | 89 | 0xC997 | 0xC9C0 | 0xC7B5 | 0x902D | 0xA140 | 0xC56F | 3.0・256 B | 版 4・128 B | 必須 | `ad102`（ad103〜ad107 も） | booter 型 |
| Blackwell（GB10x） | 100 | 0xCD97 | 0xCDC0 | 0xC9B5 | 0x902D | 0xCD40 | 0xC96F | 5.0・384 B | 版 4・128 B | 必須 | `gb100` 系 | FMC 型（別の起動の仕組み） |
| Blackwell（GB20x） | 120 | 0xCE97 | 0xCEC0 | 0xCAB5 | 0x902D | 0xCD40 | 0xCA6F | 5.0・384 B | 版 4・128 B | 必須 | `gb202`（gb203〜gb207 も） | FMC 型 |

- Hopper（GH100、SM90）は display を持たない datacenter の GPU なので範囲の外（RTX の製品が無い）。起動は Blackwell と同じ FMC 型。
- display の class（Turing）: root 0xC570、core channel 0xC57D、window channel 0xC57E、window immediate 0xC57B、cursor 0xC57A。usermode（doorbell の窓）の class は Turing 0xC461（他の世代は作業の文書）。

shader の命令（compiler、p008）:

- 命令は **SM70 以降**の全世代で 128 bit。SM70 以降は 1 つの encoder の系統で、SM の番号で分岐する。汎用 register 253、predicate 7。uniform register は Turing〜Hopper 63、Blackwell 80。
- 命令の latency の表は世代ごと（Turing 専用、Ampere と Ada で共通、Blackwell）。
- Turing → Ampere の差は小さい（warp 単位の整数の reduce、fp16 の min/max、uniform predicate の分岐などの命令の追加、compute の記述子 2.2 → 3.0）。
- Blackwell の差は大きい（ALU の operand に定数 buffer を直接書けない、texture は bindless だけ、SM120 で制御の bit の一部が無い、記述子 5.0、tiling の新しい形、depth と stencil の別 plane）。
- Mesa の NVK は Turing で Vulkan 1.4 の conformant。Turing は NVK の「新しい側の道」の下限で、Turing より前の道を全部省ける。

**最初の世代の提案: Turing（TU106）**（実機がある。class・compiler・GSP の起動の面で Ampere・Ada を後で足す差分は小〜中、Blackwell は compiler と GSP の起動の両方が大きい）。

## 8. 我々の interface への対応表

計画の「`struct drv_gpu_interface`」は実在の名前では `struct drv_gpu_ops`（`include/drivers/gpu/gpu.h`、`DRV_GPU_INTERFACE_VERSION` 9）。手本は i915 の zedBSD の書き換えの ops の表の組み立て（`src/drivers/gpu/i915/session.h` の capability の集合、`session.c`・`resource.c`・`command.c`・`job.c`・`reset.c`・`display/display.c`・`display/present.c`・`display/scanout.c`）。登録の検査は `src/drivers/gpu/gpu.c` の register の検査（capability の bit と callback の組の必須）に従う。

### 8.0 zedBSD の Vulkan の形（重大の指摘 1、判断の項目 16）

zedBSD の libvulkan（`userland/desktop/libvulkan/`）は Venus の protocol の client で、GPU の vendor の capset を読み、Vulkan の command の stream を kernel に送る。i915 は kernel の中に Vulkan の executor（`src/drivers/gpu/i915/render/`）と SPIR-V の compiler（`src/drivers/gpu/i915/compiler/`）を持ち、native でない stream を executor の命令として decode して GPU の batch に直す。Guardrail も「SPIR-V の compile は kernel 空間の driver が行う。この構成は変えない」と定める。したがって nvrtx も **kernel の中に Venus の executor と、NAK に当たる SPIR-V → SM75 の compiler を持つ**。executor は NVK の役割（pipeline・descriptor・image の tiling・compute の記述子・shader の header・MME の macro）を自前で作り、push buffer を組み立てて channel に積む。user は GPU の push buffer を直接書かない（native の stream は i915 と同じく検証で拒む）。`get_capset` は executor の capset、`get_info`（`struct gpu_info`）には chip・SM・class・VRAM の欄が無いので、それらは capset に入れる。executor と compiler は Phase 表の p008（方針）と新しい Phase または別の WS（i915 の WS031 にあたる）で作り、p007 の前提にする。userland に NVK を移植する案は Guardrail に反するので取らない。

### 8.1 capability と必須の callback（gpu.c の検査に合わせる）

最初の集合は i915 の node と同じ: `GPU_CAP_RESOURCE | GPU_CAP_TRANSFER | GPU_CAP_COMMAND | GPU_CAP_NOTIFICATION | GPU_CAP_JOB | GPU_CAP_JOB_CAPACITY | GPU_CAP_CAPSET | GPU_CAP_BLOB | GPU_CAP_MAPPING | GPU_CAP_SHARE`。display を引き継いだ node は `GPU_CAP_DISPLAY | GPU_CAP_DISPLAY_EVENTS` を足し、`display` と `scanout` の ops を付ける（i915 と同じく display の ops を bind した時だけ）。`GPU_CAP_PRESENT`・`GPU_CAP_FENCE`・`GPU_CAP_ALLOCATION_SHARE` は最初は立てない（後の Phase で判断）。recovery と scanout は capability の bit ではなく ops の表の有無。

| capability / 表 | 必須の callback（gpu.c） | nvrtx での中身 | 段 |
| --- | --- | --- | --- |
| （常に） | `get_info`、`open`・`close` | session = executor の context と、RM の client・vaspace・channel の組（session ごとに自分の GPU の仮想 address の空間）。close は RPC が失敗・timeout したら backing を解放せず隔離して fault に上げる（失敗できない契約） | P3・P5 |
| RESOURCE・BLOB | `resource_create`・`blob_create`・`resource_destroy` | buffer object。VRAM（P4 の allocator）か host の memory。session の page table に map。destroy は失敗できない: その BO を使う channel が **RM の ctrl で idle・preempt を確かめられた後**に PTE を消し TLB の invalidate の完了を待ってから返す。invalidate が時間内に終わらなければ page を返さず隔離して device の fault | P4・P5 |
| BLOB（任意） | `blob_create_placed` | VRAM の連続・整列の要求を満たす（scanout 用）。DMA32 などの system memory の配置の要求は VRAM では満たせないので ENOTSUP | P8 |
| TRANSFER | `resource_read`・`resource_write` | CPU の copy（VRAM は BAR1 の窓、host の memory は直接）。返る前に copy を終える | P4 |
| MAPPING | `resource_map`（RESOURCE か BLOB が要る） | VRAM の BO は BAR1 の窓を通す MMIO として DEVICE の属性で map。host の memory の BO は x86 の PCIe の snoop で一貫する RAM として map（未確認、p005 で確かめる）。**BAR1 の窓を使い切ったら ENOMEM**（使っていない BAR1 の map を外して空ける仕組みは後の Phase） | P4 |
| CAPSET | `get_capset` | executor の capset（libvulkan が読む Venus の vendor の capset、i915 と同じ形）。chip・SM・class・VRAM・記述子の版もここに | P3 |
| COMMAND | `command` | Venus の stream を executor の命令として decode し実行（i915 の command.c と同じ形）。native の stream は拒む | P5・P9 |
| NOTIFICATION | `commands.submit`・`commands.drain` | executor が組み立てた push buffer を session の channel の GPFIFO に積み、doorbell。完了は non-stall の割り込みで semaphore を読んで `drv_gpu_complete`。drain は session の未完了を全部待つ | P5・P9 |
| JOB（NOTIFICATION と recovery.fault が要る） | `jobs.reserve`・`jobs.commit`・`jobs.cancel` | 監督付きの job。予約の領域を前もって確保 | P5 |
| JOB_CAPACITY | `jobs.capacity` | GPFIFO の残りと software の queue の残り | P5 |
| SHARE | `share.export_resource`・`share.release`・`share.import_resource`（BLOB が要る） | 同じ node の open の間の BO の共有 | P5 |
| DISPLAY | `display.query`・`mode`・`claim`・`release`・`present`・`wait`（RESOURCE か scanout.import_image が要る） | query: RM に聞いた head・connector・EDID（RM の ctrl）と GOP の mode。mode: 最初は GOP の mode だけ。claim/release: window の lease。present: window channel の flip、完了は notifier と vblank。wait: vblank の sequence。release と fault では GOP の mode の console の surface に戻してから buffer を返す。**GSP-RM が落ちた時や console の surface に戻せない時は、scan out 中の buffer を reset まで保持する**（M11） | P8 |
| DISPLAY_EVENTS | `display.events` | vblank・hotplug の event | P8 |
| scanout の表 | `scanout.query_device`（`import_image` は任意） | constraints: VRAM の連続（display は VRAM の物理の範囲と offset で surface を指す）、offset は 256 byte、pitch は 64 byte の整列（method の field の単位から）、形式は linear と block linear。外の image の import は VRAM でないので当面 ENOTSUP（複写の道） | P8 |
| recovery の表（fault は JOB で必須） | `fault`（必須）、`stop_begin`・`stop_poll`（組）、`reset`、`isolate`（stop_begin が要る） | 8.3 | P6 |

### 8.2 安全の模型（H2・M5、判断の項目 11）

- 8.0 の形では user は push buffer を書かないので、下の問題は native の stream を許す場合（debug の道）と、executor の誤りの場合の多重の防御。
- **問題**: native の push buffer は自分の channel の vaspace に map された物なら何でも GPU に書かせられる。fence の semaphore を全 channel の vaspace に map すると（nouveau の形）、user が他の session の fence を偽造できる。fence の値で buffer の解放を決めると、解放した VRAM が別の session に再利用された後も GPU が書き続けうる。
- **案**: (a) native の push（GPU の command を直接書く道）を信頼する client（root・compositor）だけに許す（UAPI を足さず、既存の command・job の記述の中の backend の形式で運ぶ）。(b) kernel の構造（他の session の fence・USERD・page table・RM の memory）を user の vaspace に map しない。自分の fence は自分の vaspace にだけ map し、偽造しても自分の完了が狂うだけにする。buffer の解放は fence ではなく RM の channel の idle・preempt の確認で決める。
- **既定**: native の stream は拒み、立ち上げの debug に要るなら root だけ（(a)）。(b) は executor の道でも満たす（多重の防御）。UAPI（`include/uapi/` の新しい header）は足さない（i915 と同じく Venus の stream と backend の形式）。

### 8.3 回復（H3、判断の項目 9）

- `stop_begin`: その session の channel の停止を RM に頼む（非同期の RPC、待たない）。`stop_poll`: RM の応答と channel の idle・preempt が確かめられたら 0、未完了は EAGAIN、RPC の error・timeout は EAGAIN 以外（device の fault へ）。
- `isolate`: gpu.h の契約（stop_poll で停止を確かめられなかった context を隔離し、他は動き続ける）に合わせる。stop_poll で確かめられた context は isolate の対象にならない。isolate はその session の vaspace・page table・BO・instance block・USERD・GPFIFO を全部 device が持ったままにし、doorbell を拒む。**channel を無効化し preempt を RM で確かめられた時だけ 0**、確かめられなければ失敗を返して device 全体の fault にする。isolate の後の `resource_destroy`・`close` は RPC を出さず hardware に触らずに終え、回収は checked reset の時だけ（reset は P7 と再起動なので display も落ちる）。
- `fault`（core が呼ぶ）: 冪等。新しい投入を止め、全ての destroy・close が不確かな DMA（VRAM・host の page）を保持するようにする。driver が自分で見つけた異常（GSP-RM の異常・RPC の timeout）は、まず同じ隔離を構えてから `drv_gpu_report_error` で device の喪失を publish する（役割が違う）。
- `reset`: 全ての旧い所有者の退去の後に、P7 の停止と P1〜P5 の再起動。FLR が無い（Turing は無いことが多い、未確認）場合、bus reset で WPR2 と devinit の完了の印が戻らなければ、電源の切断まで fault のまま（判断の項目 14）。
- GSP-RM が落ちたときの回復を nouveau は持たない（自前で作る）。最初は fault と電源の切断まで。

## 9. BLOB（firmware の package、M6）

`userland/firmware/README.md` の規約に合わせ、GSP の firmware は**既定 off の独立の firmware の package**（例 `userland/firmware/nvidia-gsp/`、package の名前は p004 で決める）にする。base の system と kernel に同梱しない。

| file（linux-firmware の名前） | install 先 | 扱い |
| --- | --- | --- |
| `nvidia/tu102/gsp/gsp-570.144.bin`・`bootloader-570.144.bin`・`booter_load-570.144.bin`・`booter_unload-570.144.bin` | `/lib/firmware/nvidia/tu102/gsp/`（`tu104`・`tu106` の名前は linux-firmware と同じく link） | binary は変えない。NVIDIA の MIT の header に定義された container（ELF の section・bin の header）だけを読み、disassemble しない（license が reverse engineering を禁じる） |
| `nvidia/tu102/acr/bl.bin` | `/lib/firmware/nvidia/tu102/acr/`（同上の link） | nouveau では ACR の側の一覧にあり、GSP の側の一覧に無いので入れ忘れやすい |
| `LICENCE.nvidia`・`WHENCE` の該当の行・manifest | package の決まりの場所 | license の写しを添える（再配布の条件）。license の要点: 変えない binary の再配布、reverse engineering・翻訳・貸与の禁止、特許の訴えでの権利の停止、輸出の規制（監査の文書の判定の要点 5） |
| `gen_bootloader-570.144.bin` | 入れない | Turing の起動の道で使わない |
| VBIOS の FWSEC | — | GPU の ROM から実行時に読む。同梱しない |

- 取得元: linux-firmware の tag `20260410`（commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120`、AX211・i915 の package と同じ tag）。上の 5 つの file の SHA-256 は p001 で調べた commit `d947e4e8` の物と同じ（2026-10-04 に確かめた、[監査](nvrtx-license-audit.md)）。tag では `LICENCE.nvidia` は repository の直下にある。
- source の中の BLOB: nouveau の古い世代（Fermi 以前）の falcon の microcode の header はあるが、Turing の GSP の道では使わない。p009 で確かめる。

## 10. 危険と未知（実機で観測していない）

1. FWSEC の記述子の版（v2 の見込み）と、FWSEC-FRTS の失敗の error code の意味（正本に表が無い）。
2. VRAM の大きさの読み、VGA の workspace の位置で WPR2 の位置が変わること、WPR2・heap と GOP の framebuffer が重ならないか。
3. TU106 の FLR の有無、BAR の大きさ。WPR2 が残ったときの回復は停止の手順か電源の切断だけ。WPR2 が FRTS の後に booter_unload だけで外れるか、secondary bus reset で WPR2 と devinit の完了の印が戻るか（未確認）。
4. IOMMU・VFIO の下の DMA（GSP-RM が動く間ずっと map を保つ）。
5. GSP-RM の起動の後に GOP の画面がどうなるか、BAR1 の切り替えで GOP の framebuffer の窓がどうなるか（2 節の 7）。
6. nouveau は 570.144 でも一部 535 の struct を使っている。570.144 の header に合わせ、形の一致は実機で確かめる。
7. RM が channel group（TSG）を暗黙に作るか、subcontext の割り当て、doorbell の token の取り方（RM 570 の Turing の channel で token の ctrl が効くか未確認）。
8. CPU の sequencer の要求がいつ来るか。全種を実装する前提にする。
9. Turing の display が system memory から scan out できるか（未確認、できなければ scanout は VRAM だけ）。
10. 圧縮の kind の comptag。最初は非圧縮の kind だけにする。
11. RM の handle の衝突（userspace の選んだ handle を RM に渡さず、kernel が handle を割り当てる）。
12. ELF の section と firmware の file の header は、load に要る欄（大きさ・offset・section の名前と範囲）だけを読む。中身は解析しない。
13. RPC の struct の layout・queue の header と pointer の誤りは、実機では「無反応」としか出ない。**実機の前に host の試験**を置く（p004）: open-gpu-kernel-modules 570.144 の MIT の header を host で compile し offsetof・sizeof を比べる static assert、cmd・msg の queue を模擬する単体試験、WPR の layout の計算の試験（8 GiB の例で heap 105 MiB）。

## 11. 判断の項目

| # | 問い | 案・既定 |
| --- | --- | --- |
| 1 | 最初の対象の世代と chip: Turing の TU106（RTX 2070）でよいか。実機が RTX 2070 SUPER（TU104）なら同じ手順で進めてよいか | Turing。p002 で boot0 を読んで確かめ、TU104 でも進める |
| 2 | nouveau・open-gpu-kernel-modules・open-gpu-doc が大部分 MIT であることを受けて、MIT の定義（class・ctrl・RPC の struct、register）を出典付きで取り込んでよいか。Guardrail の WS037 の方式（作業の文書は temp、定数は一括で改名、最後に類似の監査）は維持する。表記の無い file と GPL の file・部分は取り込まない | MIT の file だけを出典にし、表記の無い file は読むだけ。取り込んだ file は i915 の前例（`src/drivers/gpu/i915/intel/commands.h`: SPDX MIT・元の著作権表示・変更の注記・出典の hash）の形にする |
| 3 | GSP-RM の道だけを作り、Turing の GSP 無しの道は作らない | GSP の道だけ（clock を上げられ、Ampere・Ada と同じ booter 型の道） |
| 4 | GSP の firmware を 570.144 に固定する（535.113.01 は扱わない） | 570.144 |
| 5 | GSP の firmware を既定 off の firmware の package（`userland/firmware/` の規約、`/lib/firmware` へ、LICENCE.nvidia・WHENCE・manifest 付き）にしてよいか（再配布の条件: OSI の open source の OS、binary を変えない、license の写しを添える） | そうする（zedBSD は Zlib で OSI の license） |
| 6 | 試験の方式（A: USB から素で起動、B: VFIO）と、B で guest に emulated な display を付けて段の印を撮る形でよいか。RTX の出力の撮り方。方式 A で GSP の起動が止まった時の証拠の残し方 | B を主に、N1・text console の切り離し・P8 は A で。RTX の出力はユーザーの写真か capture。A の証拠は scratch の register の段の番号を次の起動で読む案 |
| 7 | display: GOP の画面は GSP の起動で消えうる。消える前提で、P8 で RM の display を使い自前の surface に出す形でよいか（GOP の timing は RM と ARMED の register から読む）。出力先は GOP の出力先に限る（2 節の 0 の規則） | そうする |
| 8 | shader の compiler（SPIR-V → SM75）は別の WS にするか（p008） | 別の WS（i915 の WS031 と同じ形）を提案 |
| 9 | session ごとの隔離（`isolate`）を提供する条件 | 資源を全部 device が保持し、channel の無効化と preempt を RM で確かめられた時だけ成功、close・destroy は RPC を出さない（8.3）。P6 の後 |
| 10 | suspend・resume、圧縮、MST、HDMI の音は範囲の外とし、後の Phase にする | そうする |
| 11 | 安全の模型: native の push を誰に許すか、UAPI（`include/uapi/` の新しい header）を足すか（8.2） | native の stream は拒む（debug に要るなら root だけ）、UAPI を足さない。kernel の構造の分離と RM の idle の確認による解放は executor の道でも満たす |
| 12 | text console から GOP の surface を外し付け替える新しい口が pcat の text（`src/drivers/platform/pcat/`、WS037 の範囲の外）に要る（計画に無い依存、5 節） | Q1 が担当の WS を決める（新しい Phase か WS037 の範囲の拡大）。それまでは p004 の GSP の起動を方式 B だけで試す |
| 13 | firmware の package の形と linux-firmware の tag | tag `20260410`（AX211・i915 と同じ）、package は p004 で作る |
| 14 | FLR が無く bus reset で WPR2 が戻らない場合、電源の切断まで device の fault のままにすることを受け入れるか | 受け入れる（自動の再試行はしない） |
| 15 | 試験の host と、WPR2 が残った時の電源の切り方、host の操作（vfio-pci への付け替え・host の表示の停止）の承認。ユーザーの予定は centris（開発の host）だが、WPR2 が残るか GPU が固まると全 agent の共有の host の電源の切断が要る | 危険をユーザーに示し、centris のままか専用の host にするかを決めてもらう（host.md、p002） |
| 16 | zedBSD の Vulkan の形（8.0）: kernel の中に Venus の executor と SPIR-V → SM75 の compiler を作る（i915 と同じ、Guardrail の「SPIR-V の compile は kernel 空間」に沿う）でよいか。executor と compiler を WS037 の Phase にするか別の WS にするか | kernel の executor と compiler。別の WS（i915 の WS031 にあたる）を提案、p007 の前提 |
| 17 | reset（P7 と再起動）は display も落とす。display を claim した session に device の喪失を publish し、reset の後に P8 を作り直す形でよいか | そうする |
| 18 | system info の「primary の GPU か」の欄をどうするか（RM が GOP の画面を保つかに関わる可能性、推測） | 方式 A（RTX が primary）で true、B で false を試し、P8 の引き継ぎの結果で決める（p004・p006） |

## 12. Phase への引き継ぎ

- p002: host の情報の後に N0 の値（PCI・BAR・boot0・FLR・IOMMU の group）を読むだけで調べ、試験の方式を決める（判断の項目 1・6・15）。
- p003: 定数の一括の改名（temp）、骨格（PCI・BAR・`CONFIG_DRIVER_PCI_NVRTX`）、段の印、N0・N1。
- p004: P0〜P3・P7（GSP の起動の準備・起動・停止、非同期の RPC の担当、上限の大きい firmware の読み込み）、firmware の package、実機の前の host の試験（10 節の 13）。判断の項目 3〜5・12〜14・18。
- p005: P4〜P6（VRAM の allocator・MMU・BAR1/BAR2・golden context と scrubber・channel・fence・回復）。判断の項目 9・11。
- p006: P8（display）。p005 の VRAM の allocator と BAR1 の map に依存。判断の項目 7・12。
- p007: P9 と `drv_gpu_ops` への統合。executor と compiler（判断の項目 16）に依存。
- p008: executor と compiler の方針（判断の項目 8・16）。p009: 規約・license・類似の監査（MIT の表示の義務を含む）、BLOB の確認。
