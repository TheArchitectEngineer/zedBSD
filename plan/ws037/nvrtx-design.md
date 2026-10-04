<!-- awesome-plan project=zedbsd record=ws037-design -->
# nvrtx の設計（ws037-p001）

作成: 2026-10-04、P2（q692）。状態: 設計の案（code は無い）。

この文書は GPL の code・comment・定数の名前を含まない。手順は事実だけを自分の言葉で書く。class の番号・SM の番号・RPC の番号・register の offset は事実の数値として、段の設計に要る最小限だけを書く（[Guardrail](../guardrail.md) の WS141・WS037 の節）。値の一覧と手順の詳細は commit しない作業の文書にある。正本の版・hash・license は [nvrtx-license-audit.md](nvrtx-license-audit.md)。

| 作業の文書（`plan/ws037/temp/`、commit しない） | 中身 |
| --- | --- |
| `gsp-boot.md` | PCI の発見・chip の判定・BAR・reset、devinit の待ち、VBIOS と FWSEC、WPR2、GSP の firmware の section・page table・args・queue、booter、RISC-V の起動、RPC の形と番号、RM の object、停止・suspend・resume、firmware の file、各段の確かめる値 |
| `channel-mmu-display.md` | Turing の MMU の page table、vaspace と BAR1・BAR2、channel の作り方と doorbell、push buffer・GPFIFO・fence、class と golden context、RC と回復、NVDisplay の class・初期化・GOP の readout・mode set・flip・vblank・EDID と link、NVK が kernel に求める物 |
| `mesa-generations.md` | NVK・NAK の世代の差（class・記述子・shader header・encoder・latency の表・Vulkan の機能）、最初の世代を選ぶ材料 |

どの文書も source を読んだだけで、**実機・QEMU での観測は無い**。

## 1. 最初の対象と構成

### 1.1 最初の対象: TU106（RTX 2070、Turing、SM75）

- chip の判定は PCI の device ID ではなく、BAR0 の先頭の register（boot0）の bit 28:20 が 0x166 であることで行う（nouveau も device ID の表を持たない）。device ID は表示にだけ使う。
- NVIDIA の open-gpu-kernel-modules の README の表で、RTX 2070 は 0x1F02・0x1F07（desktop）。0x1F00〜0x1F7F が TU106 と推定（README に chip の名前は無い）。
- **RTX 2070 SUPER は TU104**（0x1E84・0x1EC2・0x1EC7、推定）で TU106 ではない。nouveau では同じ GSP の手順と同じ firmware（tu104 も tu102 の物を指す）なので、TU104 でも手順は同じ見込み。実機がどちらかは p002 で確かめる（判断の項目 1）。
- boot1 の bit 17:16 が立っていれば vGPU として扱わない。

### 1.2 構成

| 部品 | 役割 | 要点 |
| --- | --- | --- |
| BAR0 | register の窓 | 先頭の register で chip の判定。PCI の config space の写しが 0x088000〜にある |
| BAR1 | VRAM の窓 | GSP の下では BAR1 の page table の根を GSP-RM が持つ |
| BAR2(PCI の BAR3) | instance memory の窓 | kernel が page table を作り、根を RPC で GSP-RM に知らせる。前半だけを kernel が使う |
| GSP（falcon と RISC-V） | GSP-RM（NVIDIA の resource manager）を走らせる | firmware は file から load。kernel と共有 memory の queue（cmd・msg）で RPC |
| SEC2（falcon） | 署名付きの booter（起動・停止の ucode）を走らせる | booter が GSP-RM の image を WPR2 に置いて GSP を起こす |
| VBIOS | FWSEC（署名付き ucode）を含む | FWSEC-FRTS が VRAM の末尾に WPR2 を張る。停止では FWSEC-SB |
| VRAM の末尾 | WPR2（GSP-RM の image・heap）と VGA の workspace | 8 GiB の例で WPR2 約 135 MiB、heap 約 105 MiB（式は作業の文書） |
| DMA | GPU が host の memory を読む | 47 bit の address。GSP に渡す番地は全部 bus（DMA）番地で、GSP-RM が動く間ずっと有効でなければならない |

### 1.3 何を GSP-RM がし、何を kernel がするか（Turing・570.144 の GSP の道）

| GSP-RM（RPC で頼む） | kernel（zedBSD）が自分でする |
| --- | --- |
| object の木（client・device・subdevice・vaspace・channel・engine の object）、channel の schedule、GR の golden context、RC（channel の error）の検出、display の SOR の割り当て・DP の link training と AUX・EDID・HDMI・supervisor の処理、BAR1 の page table | GPU の仮想 address の page table（5 段、4K・64K・2M の page）と TLB の invalidate、BAR2 の page table、instance block・USERD・GPFIFO・push buffer の memory、doorbell、fence の semaphore と non-stall の割り込み、display の core・window channel の push buffer（mode・surface・flip）、vblank の割り込み |

## 2. 方針の要点

1. **GSP-RM の道だけを作る**（判断の項目 3）。Turing は GSP 無しでも nouveau で動くが、GSP 無しの道は clock の管理（reclocking）を持たない見込み（推論、未確認）なので、GSP の道を取る。Ampere・Ada・Blackwell は GSP が必須で、同じ道の延長になる。
2. **firmware の版は 570.144 に固定**（判断の項目 4）。RPC の struct は版ごとに変わるので、版を混ぜない。struct の定義は open-gpu-kernel-modules 570.144（MIT）から取る。
3. **段ごとに範囲を限る**。N（読むだけ）と P（書く）を分け、書く段の前に段の印を出す（5 節）。WPR2 を張った後に失敗したら、停止の手順（booter_unload）で WPR2 を解いてから再試行する（WPR2 が残ると次の FWSEC-FRTS が失敗する見込み）。load の前に WPR2 の register を読み、残っていれば止まる（nouveau は読まない）。
4. **IOMMU・DMA の寿命**: GSP に渡す memory（firmware の page、page table、args、log、queue、FWSEC の ucode）は、GSP-RM が動く間ずっと map と物理を保つ。driver の終了と device の fault でも、GSP を止めるまで返さない（`drv_gpu_ops` の recovery の契約と同じ考え）。
5. **GOP の画面は GSP の起動で消えうる前提で設計する**（GSP の起動で BAR1 が GSP-RM の page table に切り替わり、GOP の framebuffer の物理の窓が見えなくなる危険）。段の印は P1 の前と、display の引き継ぎ（P6）の後で出し先が変わる（5 節）。

## 3. 段の一覧

i915 の名付けに合わせる（N = GPU の状態を変えない、P = 変える）。WS037 の ws.md の段（N0・N1・P1〜P5）を、作業の文書の結果で細かくした。

| 段 | すること（自分の言葉で） | 確かめる値 | Phase |
| --- | --- | --- | --- |
| N0 | PCI の発見、BAR の大きさと種類、BAR0 の map、boot0・boot1 で chip の判定、PCI の FLR の能力の bit を読む | boot0 の chip = 0x166（TU104 なら 0x164）、boot1 の vGPU の bit が 0、BAR0 の読みが全 1 でない、BAR の大きさ（未知、記録する） | p003 |
| N1 | GOP の画面の readout（読むだけ）: loader が渡す GOP の情報（幅・高さ・stride・物理）を記録。display の register の ARMED の状態（timing・surface）を読めるか試す | GOP の framebuffer の物理が BAR1 の窓の中にあるか。読めた timing が GOP の大きさと合うか | p003 |
| P1a | devinit の完了待ち: VBIOS の script は実行せず、firmware（GFW）の devinit が終わった印を待つ | 完了の bit と進み具合の値が「完了」（数秒以内） | p004 |
| P1b | VBIOS を読む（PRAMIN か PROM から）、FWSEC の image を取り出す | image の署名、FWSEC の記述子の版（v2 の見込み、未確認） | p004 |
| P1c | WPR2 が張られていないことを確かめ、FWSEC-FRTS を GSP の falcon で走らせる（`acr/bl.bin` を bootloader に） | falcon の停止、scratch の error code が 0、WPR2 の下限・上限が 0 でなくなる | p004 |
| P2a | GSP-RM の image（firmware の ELF の section）・署名・page table（3 段）・libos の args・RM の args・queue・log を host の memory に用意し、booter_load を SEC2 で走らせる | SEC2 の mailbox が 0、falcon の停止 | p004 |
| P2b | GSP の RISC-V の起動を確かめ、system info と registry の RPC を先に積み、初期化の完了の event を待つ | RISC-V の状態の bit、初期化の完了の event（0x1001）が数秒以内、log の buffer の put | p004 |
| P3 | static info の RPC、RM の object（client → device → subdevice）、割り込みの表（kernel 用の vector）、BAR2 の根の通知 | 各 RPC の status が 0、static info の長さ | p004 |
| P4 | MMU（自前の page table を作り外部所有の vaspace として RM に登録）、channel（instance block・USERD・GPFIFO・method buffer を用意して RM に確保させ、bind と schedule）、doorbell、fence（semaphore の release と non-stall の割り込み） | 何もしない push の後に semaphore の値が進む、non-stall の割り込みが来る | p005 |
| P5 | reset と回復: RC の event で channel を消す。GPU 全体は停止の手順（P7）と再起動 | RC の event の受け取り、channel の再作成 | p005 |
| P6 | display: RM の display の object・core channel・window channel を作り、GOP の画面の head・SOR を引き継ぎ（RM に聞く）、自前の surface（VRAM）を window に出す。flip は window channel の offset の更新と update、完了は notifier、vblank は head の timing の割り込み | 画面に自前の surface、notifier の完了、vblank の回数が refresh に合う | p006 |
| P7 | 停止: unload の RPC → GSP の mailbox の完了の値 → FWSEC-SB → booter_unload | WPR2 の上限が 0 に戻る | p004 |
| P8 | 最初の 3D・compute・copy の job: 各 subchannel に class の object を bind し、GR の context を RM に渡して 3D の object を作り、copy の fill と compute の空の dispatch を流す | semaphore の進み、copy の結果 | p007 |

- Turing と 570.144 では GR の初期化の前に scrubber の channel が要る（RM の既知の回避）。p005 で扱う。
- suspend・resume は範囲の外（後の Phase）。Turing の suspend には 50 ms の待ちの回避がある（作業の文書）。

## 4. command の投入の順（自分の言葉で）

1. push buffer に method を書く（header は「連続の register に増えながら」「同じ register に繰り返し」「header の中の値だけ」「最初の 1 つだけ増える」の 4 種）。
2. GPFIFO の ring に entry（8 byte、push buffer の GPU の仮想 address と長さ）を書く。
3. USERD の GP_PUT を進める → BAR1 の書き込みを読み戻しで確定 → doorbell の register に channel の token を書く。token は RM に聞いて得る（nouveau は自分で組み立てているが、RM の ctrl で取る方が安全、未確認）。
4. 完了: push の最後に host の class の method で semaphore の release（値と address、wait-for-idle 付き）→ system の memory barrier → non-stall の割り込み。kernel は割り込みで semaphore の値を読み、fence を完了させる。
5. 3D・compute・copy の class は subchannel ごとに最初に object を bind する。GR の context は RM が golden context を作り、kernel が context の buffer を用意して RM に渡す。

display の flip: window channel の push buffer に surface の offset（VRAM の物理の範囲の context と offset）・大きさ・形式を書き、update を送り、push buffer の PUT を進める。完了は notifier の memory の状態を見る。

## 5. 段の印の設計

- **出し方**: i915 と同じく driver が段の始めと終わりに `kern_logf` で行を出す（例 `nvrtx: N0 begin`、`nvrtx: N0 ok chip=166`、`nvrtx: P2b ok`）。amd64 では console の文字が GOP の framebuffer の text console（`src/drivers/platform/pcat/graphics/text.c`）に出る。1 行は console の桁に収める。
- **危ない段の前で待つ**: P1c（FWSEC-FRTS）・P2a（booter）・P6（display）の前に begin の行を出し、数秒待つ。画面が消えても写真・screendump に begin の行が残る。
- **段で止める boot の parameter**: 例 `nvrtx.stop=P1c`、`nvrtx.off=1`。1 段ずつ進める。
- **GSP の起動の後**: GOP の画面が消えたら、P6 で display を引き継いだ後に自前の surface に console を描き直す（i915 の resident display の console と同じ考え）。P2〜P5 の間は画面に印が出ない可能性があるので、試験の方式 B（VFIO）では guest に QEMU の emulated な display も付け、そちらを主の console にして段の印を出し、QMP の screendump で撮る（判断の項目 6）。
- **text console の扱い**: GOP の framebuffer を使う text console は、GSP の起動の前に止め（`drv_pcat_text_suspend` に当たる口を i915 の引き継ぎと同じ形で使う、p003 で確かめる）、P6 の後に自前の surface で再開する。

## 6. 試験の道

| 方式 | 中身 | 印の見方 | 注意 |
| --- | --- | --- | --- |
| A: USB から素で起動 | amd64 の image に nvrtx の段の印の kernel を入れて、RTX 2070 の host を USB から起動 | ユーザーの写真か capture の器具（RTX 2070 の出力） | WPR2 が残ったまま失敗すると、host の電源を切るまで再試行できない見込み |
| B: VFIO | 開発の host（centris）の Linux で RTX 2070 の全 function（video・audio・USB・UCSI）を vfio-pci に付け、QEMU の guest に渡す（WS029 p006 の手順が手本） | guest の emulated な display に段の印を出し、QMP の screendump で撮る（AGENTS.md の screendump の道）。RTX の出力は写真 | host が一度も GPU を使わない状態で渡す。FLR の有無（未確認）と、reset の後に devinit の完了の印と WPR2 が戻るか（未確認）。QEMU の console・serial の log は判定に使わない |

host の情報は [host.md](host.md)（ユーザーの情報待ち）。p002 で決める。

## 7. 世代の差（事実の表）

Mesa 25.3.6 と Linux v6.19 の nouveau の class の表を読んだ結果（2026-10-04）。

| 世代（chip） | SM | 3D | compute | copy | 2D | inline-to-memory | channel（GPFIFO） | compute の記述子 | graphics の shader header | Linux の nouveau で GSP が必須か |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Turing（TU10x・TU11x） | 75 | 0xC597 | 0xC5C0 | 0xC5B5 | 0x902D | 0xA140 | 0xC46F | 2.2・256 B | 版 4・128 B | 必須でない |
| Ampere（GA100） | 80 | 0xC697 | 0xC6C0 | 0xC6B5 | 0x902D | 0xA140 | 0xC56F | 3.0・256 B | 版 4・128 B | 必須 |
| Ampere（GA10x） | 86 | 0xC797 | 0xC7C0 | 0xC7B5 | 0x902D | 0xA140 | 0xC56F | 3.0・256 B | 版 4・128 B | 必須でない |
| Ada（AD10x） | 89 | 0xC997 | 0xC9C0 | 0xC7B5 | 0x902D | 0xA140 | 0xC56F | 3.0・256 B | 版 4・128 B | 必須 |
| Blackwell（GB10x） | 100 | 0xCD97 | 0xCDC0 | 0xC9B5 | 0x902D | 0xCD40 | 0xC96F | 5.0・384 B | 版 4・128 B | 必須 |
| Blackwell（GB20x） | 120 | 0xCE97 | 0xCEC0 | 0xCAB5 | 0x902D | 0xCD40 | 0xCA6F | 5.0・384 B | 版 4・128 B | 必須 |

display の class（Turing）: root 0xC570、core channel 0xC57D、window channel 0xC57E、window immediate 0xC57B、cursor 0xC57A。

shader の命令（compiler、p008）:

- 命令は全世代 128 bit。SM70 以降は 1 つの encoder の系統で、SM の番号で分岐する。汎用 register 253、predicate 7。uniform register は Turing〜Hopper 63、Blackwell 80。
- 命令の latency の表は世代ごと（Turing 専用、Ampere と Ada で共通、Blackwell）。
- Turing → Ampere の差は小さい（warp 単位の整数の reduce、fp16 の min/max、uniform predicate の分岐などの命令の追加、compute の記述子 2.2 → 3.0）。
- Blackwell の差は大きい（ALU の operand に定数 buffer を直接書けない、texture は bindless だけ、SM120 で制御の bit の一部が無い、記述子 5.0、tiling の新しい形、depth と stencil の別 plane）。
- Mesa の NVK は Turing で Vulkan 1.4 の conformant。Turing は NVK の「新しい側の道」の下限で、Turing より前の道を全部省ける。

**最初の世代の提案: Turing（TU106）**（実機がある。class と compiler の面で Ampere を後で足す差分は小〜中、Ada は Ampere とほぼ同じで GSP が必須なだけ、Blackwell は大きい）。

## 8. 我々の interface への対応表

計画の「`struct drv_gpu_interface`」は実在の名前では `struct drv_gpu_ops`（`include/drivers/gpu/gpu.h`、`DRV_GPU_INTERFACE_VERSION` 9）。手本は i915 の zedBSD の書き換えの ops の表の組み立て（`src/drivers/gpu/i915/` の `session.c`・`resource.c`・`command.c`・`job.c`・`reset.c`・`display/display.c`・`display/present.c`・`display/scanout.c`）。

| zedBSD の口 | nvrtx での中身 | 段 |
| --- | --- | --- |
| `drv_gpu_register` | GPU 1 枚 = 1 device（display と描画は同じ GSP-RM の下にあり、GSP-RM が落ちると両方落ちるので、分けても隔離にならない） | N0 |
| `capabilities` | resource・transfer・command・job（notification 付き）・recovery・display・scanout | P3〜P6 |
| `open` / `close` | session = RM の client と vaspace と channel の組（session ごとに自分の GPU の仮想 address の空間） | P4 |
| `get_info` / `get_capset` | chip・SM・class の番号・VRAM の大きさ（static info） | P3 |
| `resource_create` / `blob_create` / `resource_destroy` | buffer object。VRAM（GPU の VRAM の allocator、kernel が管理する範囲）か host の memory。session の page table に map。destroy は、使う job が終わった後に PTE を消し TLB の invalidate の完了を待ってから返す（失敗できない契約） | P4 |
| `blob_create_placed` | VRAM の連続・整列の要求を満たす（scanout 用） | P6 |
| `resource_map` | VRAM の BO は BAR1 の窓を通す MMIO として DEVICE の属性で map。host の memory の BO は x86 の PCIe の snoop で一貫する RAM として map（未確認、p005 で確かめる） | P4 |
| `commands`・`jobs` | 自前の job の記述（push buffer の範囲の列・fence の値）を受け、session の channel の GPFIFO に積み、doorbell。完了は non-stall の割り込みで semaphore を読んで `drv_gpu_complete`。記述の UAPI の header は `include/uapi/`（version・size・reserved = 0・64 bit の整列） | P4・P8 |
| `recovery`（`stop_begin`・`stop_poll`・`fault`・`reset`） | stop は channel の schedule を止め（RM の ctrl）、channel が idle になるまで EAGAIN。GPU 全体の fault は GSP-RM の異常のとき。reset は停止の手順（P7）と再起動（P1〜P3）で、失敗すれば PCI の reset（未確認） | P5 |
| `recovery.isolate` | **提供できる見込み**: session ごとに vaspace と channel があり、RC の event で 1 つの channel だけを消せる。消した channel の memory は reset まで device が持つ | P5 |
| fence（`gpu-fence.h`） | channel ごとの semaphore の値 | P4 |
| `display` | query: RM に聞いた head・connector・EDID（RM の ctrl）と GOP の mode。mode: 最初は GOP の mode だけ。claim/release: window の lease。present: window channel の flip、完了は notifier と vblank。release と fault では GOP の mode の console の surface に戻してから buffer を返す | P6 |
| `scanout` | constraints: VRAM の連続（display は VRAM の物理の範囲と offset で surface を指す）、offset は 256 byte、pitch は 64 byte の整列（method の field の単位から）、形式は linear と block linear。外の image の import は VRAM でないので当面 ENOTSUP（複写の道） | P6 |

## 9. BLOB

| file（linux-firmware の名前） | 置き場所の案 | 扱い |
| --- | --- | --- |
| `nvidia/tu106/gsp/gsp-570.144.bin` ほか bootloader・booter_load・booter_unload（実体は tu102） | `userland/firmware/nvidia-gsp/tu102/`（tu106・tu104 は同じ物を指す） | `LICENCE.nvidia` を添えて同梱。binary は変えない。中身は解析しない |
| `nvidia/tu106/acr/bl.bin`（実体は tu102） | 同上の `acr/` | nouveau の module の firmware の一覧に無く、入れ忘れやすい |
| VBIOS の FWSEC | GPU の ROM から実行時に読む | 同梱しない |

source の中の BLOB: nouveau の古い世代（Fermi 以前）の falcon の microcode の header はあるが、Turing の GSP の道では使わない。p009 で確かめる。

## 10. 危険と未知（実機で観測していない）

1. FWSEC の記述子の版（v2 の見込み）と、FWSEC-FRTS の失敗の error code の意味（正本に表が無い）。
2. VRAM の大きさの読み、VGA の workspace の位置で WPR2 の位置が変わること、WPR2・heap と GOP の framebuffer が重ならないか。
3. TU106 の FLR の有無、BAR の大きさ。WPR2 が残ったときの回復は停止の手順か電源の切断だけ。
4. IOMMU・VFIO の下の DMA（GSP-RM が動く間ずっと map を保つ）。passthrough で reset の後に devinit の完了と WPR2 が戻るか。
5. GSP-RM の起動の後に GOP の画面がどうなるか、BAR1 の切り替えで GOP の framebuffer が見えなくなるか。
6. nouveau は 570.144 でも一部 535 の struct を使っている。570.144 の header に合わせ、形の一致は実機で確かめる。
7. RM が channel group（TSG）を暗黙に作るか、subcontext の割り当て、doorbell の token の取り方。
8. CPU の sequencer の要求（RM が kernel に register の操作を頼む）がいつ来るか。全種を実装する前提にする。
9. GSP-RM が落ちたときの回復を nouveau は持たない（自前で作る）。
10. 圧縮の kind の comptag。最初は非圧縮の kind だけにする。
11. RM の handle の衝突（userspace の選んだ handle を RM に渡さず、kernel が handle を割り当てる）。

## 11. 判断の項目

| # | 問い | 案・既定 |
| --- | --- | --- |
| 1 | 最初の対象の世代と chip: Turing の TU106（RTX 2070）でよいか。実機が RTX 2070 SUPER（TU104）なら同じ手順で進めてよいか | Turing。p002 で boot0 を読んで確かめ、TU104 でも進める |
| 2 | nouveau・open-gpu-kernel-modules・open-gpu-doc が大部分 MIT であることを受けて、MIT の定義（class・ctrl・RPC の struct、register）を i915 と同じく出典付きで取り込んでよいか。Guardrail の WS037 の方式（作業の文書は temp、定数は一括で改名、最後に類似の監査）は維持する。表記の無い Mesa・Linux の file は取り込まない | MIT の file だけを出典にし、表記の無い file は読むだけ |
| 3 | GSP-RM の道だけを作り、Turing の GSP 無しの道は作らない | GSP の道だけ（clock を上げられ、Ampere 以降と同じ道） |
| 4 | GSP の firmware を 570.144 に固定する（535.113.01 は扱わない） | 570.144 |
| 5 | GSP の firmware を `userland/firmware/nvidia-gsp/` に `LICENCE.nvidia` を添えて同梱してよいか（再配布の条件: OSI の open source の OS、binary を変えない、license の写しを添える） | 同梱する（zedBSD は Zlib で OSI の license） |
| 6 | 試験の方式（A: USB から素で起動、B: VFIO）と、B で guest に emulated な display を付けて段の印を screendump で撮る形でよいか。RTX の出力の撮り方 | B を主に（i915 の WS029 p006 と同じ）、RTX の出力はユーザーの写真か capture |
| 7 | display: GOP の画面は GSP の起動で消えうる。消える前提で、P6 で RM の display を使い自前の surface に出す形でよいか（GOP の timing は RM と ARMED の register から読む） | そうする |
| 8 | shader の compiler（SPIR-V → SM75）は別の WS にするか（p008） | 別の WS（i915 の WS031 と同じ形）を提案 |
| 9 | session ごとの隔離（`isolate`）を最初から提供するか | 提供する（channel 単位で消せるため）。ただし P5 の後 |
| 10 | suspend・resume、圧縮、MST、HDMI の音は範囲の外とし、後の Phase にする | そうする |

## 12. Phase への引き継ぎ

- p002: host の情報の後に N0 の値（PCI・BAR・boot0・FLR・IOMMU の group）を読むだけで調べ、試験の方式を決める（判断の項目 1・6）。
- p003: 定数の一括の改名（temp）、骨格（PCI・BAR・`CONFIG_DRIVER_PCI_NVRTX`）、段の印、N0・N1。
- p004: P1a〜P3・P7（GSP の起動と停止）。判断の項目 3〜5。
- p005: P4・P5（MMU・channel・fence・回復）。
- p006: P6（display）。判断の項目 7。
- p007: P8 と `drv_gpu_ops` への統合。
- p008: compiler の方針（判断の項目 8）。p009: 規約・license・類似の監査、BLOB の確認。
