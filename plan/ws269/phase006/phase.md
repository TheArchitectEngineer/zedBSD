<!-- awesome-plan project=zedbsd record=ws269-p006 -->

# ws269-p006 render の node: GPU の MMU・context・queue・fence・share・recovery・coherency の確かめ（MIPS の道）

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) 改訂 3.1 の §3.2〜§3.5・§6 の p006 と §6 の末尾の必須項目
- Queue: q1337 / q1337-i01（P2、2026-10-19）
- Status: in-progress（2026-10-19 P2: 実装・build・host 試験まで。実機の確認（下の文面）は p005 の実機の結果の後。clearance は Q1 の判定）
- 前提: p005（実装済み・実機待ち）、D7 (a)（p004 の space.c の user の NOCACHE）。HAL の API は変えない。

## 目的と受け入れの条件

1. render の node（gpu1）を遅い段で登録する。late register の EBUSY は p005 の `powervr_retry_busy`（100 ms 間隔で 50 回）で再試行し、尽きたら §3.1 の失敗の道（FW を止め、IRQ を外し、memory を返し、reset・clock・PD を落とす）を通す。
2. GPU の MMU: Rogue の 3 段（catalogue 1024 × 32 bit、directory・table 512 × 64 bit）、**4 KiB の page だけ**（BRN71317）、PTE の **CC = 0**（snoop を求めない）。SLC_BYPASS は CPU が読む物に付ける（下の「決めた事」）。
3. context・queue・fence: session ごとに memory context と FW の compute context、client の CCB、timeline の sync word。native の command（空の job）を kick し、timeline が届いたら completion を返す。
4. `share`（export・import・release・get_scanout_backing）と、display の node が render の buffer を private な lookup で retain する API（P1 の display.c の import が使う、下の「display の node への口」）。
5. coherency の確かめ（**MIPS（UFO）の道**だけ。64 KiB × 8 回、2 MiB の中で 128 KiB ずつずらす、SLC の flush を入れない、0b の対照、INCONCLUSIVE なら切り替えない）。結果は 1 行で出し、共有の仕方は変えない。
6. recovery の ops を gpu.h の契約の文どおりに: stop_begin・stop_poll・fault・isolate・reset（保持した FW の image で再起動）。
7. blob の backing を連続か page ごとかの決定（design-review-3 Minor 8）。
8. user の blob の HOST_COHERENT は D7 (a)（uncached の別名、`DRV_GPU_MAPPING_UNCACHED_RAM` → p004 の space.c の NOCACHE）。
9. build（VF2・riscv64-virt・amd64）warning 0、host 試験。QEMU は起動しない。実機の試験の文面を書く（今は頼まない）。

## 実装（commit eeb033de7）

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/powervr/gpu-mmu.c`・`.h`（新規、host で build 可） | GPU の page table の 3 段（catalogue の entry は directory の page 番号を 4 bit 上げて valid、directory の entry は table の address と 4 KiB の page の大きさ 0 と valid、page の entry は address・valid・read only・SLC bypass、**CC の bit は立てない**）。map は page 単位で、既に valid な page があれば EEXIST で何も変えない、途中の失敗は書いた分を戻す。unmap は entry だけを消し、table は context が終わるまで持つ（FW が cache を捨てる前に table を返さない）。context の address の空間（general heap 0x80_0000_0000 から 128 GiB、範囲ごとに guard の 1 page、first fit、最大 512 範囲）。table と管理の memory は callback から取る |
| `commands.c`（新規） | KCCB の送り（slot に命令、return slot を 0、barrier、write offset、MTS の kick）と return slot の待ち（短い spin の後に tick ごと、上限付き）、`run`（送って待つ）、MMU の cache の invalidate の命令（PT・PD・PC・TLB、sync word に 0）、FWCCB の notice を callback に渡して取る。p005 の health check と FWCCB の読みもこれに寄せた |
| `coherency.c`（新規） | MIPS の道の確かめ（下の「coherency の確かめ」）。2 MiB の run を取り、uncached の別名と cacheable の direct map、FW の heap への MIPS の写像（MIPS 側 uncached）を作る。0b の対照、8 回の round、後始末は範囲を `kern_dcache_invalidate_range`（FLUSH64）で捨てる。結果は 1 行 |
| `render.c`（新規） | render の node の ops: open（session ごとに memory context と FW の記録）、close（FW に context の cleanup を求めてから返す。quarantine・isolate の時は reset まで保持）、get_info、resource_create（storage、host visible）、blob_create（MAPPABLE・SHAREABLE・CROSS_DEVICE、blob_id は 0 だけ）、resource_read・write（uncached の view を 8 byte 揃いの時だけ word、他は byte）、resource_map（run の全体、`DRV_GPU_MAPPING_UNCACHED_RAM`）、share（export は image があれば形と範囲を確かめ page の list を作る、import は同じ node の export だけ、backing は CONTIGUOUS の page の list）、scanout の query_device（role は RENDER、companion なし）。buffer は参照数で数え、最後の参照で解放、quarantine 中は reset まで保持。display の node への lookup の API。遅い段からの `powervr_render_start`（coherency の確かめ、service の thread、自己試験、登録の再試行）と、失敗の時の後始末 `powervr_render_abandon` |
| `render-queue.c`（新規） | session の FW の記録（memory context: catalogue の PA と register set なし。compute context: client の CCB とその control・state・data master CDM（4）・priority 0・deadline 30 s・memory context・host の識別子。timeline の sync word）。kick: [FENCE（任意）][NULL の job][timeline の UPDATE] を client の CCB に書き（末尾は PADDING）、KICK の命令（context・write offset・wrap mask）を送る。cleanup（CLEANUP の命令、BUSY なら 10 ms の間隔で 100 回まで）。service の thread（IRQ・submit・tick で起き、timeline の届いた completion を返し、FWCCB の notice を読み、deadline + 5 s を過ぎた job があれば health check を送り、答えが無ければ quarantine と `drv_gpu_report_error`）。commands の submit・drain、recovery の stop_begin・stop_poll・fault・isolate・reset、IRQ からの wake |
| `render.h`（新規） | display の node 向けの lookup の API（下） |
| `render-private.h`（新規） | node・session・resource・buffer・export の型と内部の関数 |
| `interface.h` | KICK 101・MMUCACHE 102・CLEANUP 106、return slot の CLEANUP_BUSY、MMU の cache の命令の field、FWCCB の CONTEXT_RESET 105・REQUEST_GPU_RESTART 109・FW_PF 112、FW の memory context（32 byte）・common context（152 byte）・compute context（232 byte）・compute の state（4 byte）、client の CCB の control（32 byte）と header（40 byte）、NULL（210 + task bit）・FENCE 212・UPDATE 213・PADDING 221、UFO（8 byte）、general heap |
| `mips-table.c`・`.h` | FW の heap の general part の page の map（first fit の take、指定の claim、give）。host で試せる |
| `firmware-memory.c`・`powervr-private.h` | heap の cursor（増えるだけ）を page の map に替え、block を返すと場所も空く（session が来ては去るため）。`powervr_object_drop`（stop した FW の heap の外で run だけを返す）。core に lock（KCCB・heap・context の記録）と render の pointer |
| `firmware-boot.c` | 遅い段を「file を読む」と「保持した file から起こす」（`bring_up`）に分け、`powervr_firmware_restart`（processor を止め、IRQ を外し、block を返し、電源を切って入れ直し、保持した file から起こす。root を待たず file を読まない）を足した。health check は `commands.c` を使う。成功の後に `powervr_render_start`、失敗なら §3.1 の失敗の道と `powervr_render_abandon`。IRQ の handler は render の service を起こす |
| `powervr.c` | `powervr_power_up`（power_up と register の窓の写像。identity は attach の時の物） |
| `platform/riscv64/vmunix.mk` | 新しい 5 file を `CONFIG_DRIVER_POWERVR` の時に build |
| `plan/ws269/tests/p006/`（新規） | `run.sh`・`gpu-mmu-host-test.c` |

出典は block ごとの `Value(s) referenced:` / `Procedure referenced:`。p006 で新しく参照した値・手順は vendor の tree に同梱の mainline の driver（StarFive Linux `JH7110_VisionFive2_6.12.y_devel` 4cecf169f38e の `drivers/gpu/drm/imagination/`、`~/visionfive-linux`）から取り、その版を書いた（p005 の v7.3 の値と同じ FW の interface の版の header）。code・構造・名前は写していない。

### 決めた事

- **backing の連続性（design-review-3 Minor 8）: 連続の run に決めた**。render の target・storage・blob は全部 1 本の物理で連続の run（`kern_pmem_alloc_limited`、DMA の上限の下）と、その 1 本の uncached の別名。理由: (1) 共通の map の契約（`drv_gpu_mapping` は 1 つの物理の範囲、`gpu.c:3131-3150`）が 1 本の範囲しか表せず、page ごとの確保では HOST_VISIBLE の blob を user に写せない（gpu.h の変更が要る）、(2) display の node の lookup と scanout の backing が CONTIGUOUS の 1 本で済む、(3) GPU 側は MMU が 4 KiB ずつ写すので連続性を要しない。代わりに大きな確保（上限 256 MiB / 1 個）は 4 GiB の下の断片化で ENOMEM になり得る（推測、未計測）。page ごとの確保は DEVICE_LOCAL の大きな texture が実際に失敗した時に、map を持たない blob に限って足す（p009 以降、Future Work の候補として Q1 に登録を依頼）。
- **SLC_BYPASS**: GPU の PTE は、CPU が読み書きする物（storage の resource、MAPPABLE の blob、import した物）に SLC_BYPASS = 1、map を持たない blob に 0。CC は常に 0。FW の構造（FWCCB など）は MIPS の写像で、MIPS の PTE に SLC の bit は無いので、MIPS の道の結果（下）で判断する。試験と製品で同じ値（試験の道具も製品と同じ `powervr_object_create`・`powervr_mips_map` の uncached を使う）。
- **MIPS（UFO）の道の道具**: FW の MIPS が 4 byte を書く手段として、KCCB の MMU の cache の命令の sync word（命令ごとに 1 つの FW の address に値を書く）を使う。context を要さず、FW の MIPS だけが書く。1 word に 1 命令で 16,384 命令 / round、KCCB が満ちるまで送って最後の slot を待つ。FW が sync word を書かない版なら「writes missing」で INCONCLUSIVE になる（判定に使わない）。
- **逆向き（CPU が cacheable で書き clean せず、FW が読む）**: 自己試験の中で 1 回。FW の heap の 1 page（行に他の物を置かない）に CPU が direct map で書き、その word を待つ FENCE の付いた空の job を kick。200 ms で進めば「clean なしで見えた」、進まなければ line を clean して kick し直し、進めば「clean の後だけ見えた」。結果は 1 行。
- **native の command**: Venus の decode（p009）までは、8 byte の「空の job」（opcode 0x4e4a4250・予約 0）だけを受ける。GPU_CAP_COMMAND（同期の `command`）は出さず、GPU_CAP_NOTIFICATION の `commands`（submit・drain）だけ。CAPSET を出さないので libvulkan はこの node を使わない（今の VF2 と同じく Vulkan の device は無い）。
- **role**: scanout の query_device で RENDER を返す（companion なし）。
- **coherency の結論は報告だけ**: 結果が COHERENT でも既定（非 coherent）は変えない（切り替えは別の Phase で Q1 の承認）。0b が INCONCLUSIVE なら一致しても「MATCHED WITHOUT CONTROL」と出し、切り替えの根拠にしない。
- **recovery の対象**: この Phase の session の FW の context は compute の 1 つだけ（geometry・fragment・transfer は p010）。stop_begin・stop_poll・isolate はその session の全部の FW の context（今は 1 つ）を対象にする形で書いた。stop_poll は CLEANUP の答えを待たずに読み、BUSY なら送り直し（100 回まで、超えたら EBUSY）、30 s で ETIMEDOUT（core は isolate を呼び、失敗なら fault）。cleanup が済めば timeline の届いた物を 0 で、残りを ECANCELED で返し、全部返ったら 0。isolate は session を「reset まで保持」にし、health check で FW が他を処理し続けている事を確かめてから callback を EIO で終える（答えが無ければ失敗を返し、core が fault にする）。fault は冪等で、新しい kick を止め、destroy・close を「保持」にし、待っている callback を error で返す（hardware が止まったとは言わない）。health check の時間切れ・FW の page fault・restart の要求は service が quarantine にしてから `drv_gpu_report_error`。reset は保持した FW の image で `powervr_firmware_restart` し、保持していた session の記録と buffer をここで初めて返す。
- **lock の順**: core の lock（KCCB・heap・context の記録）→ queue の lock（pending・stop の flag・faulted、IRQ からも取る）。buffer の guard は他と重ねない。IRQ の上半分は FW の lock を取らない（status を読んで clear し、service を起こすだけ）。

### display の node への口（P1 の p007 の後半の import 用）

`src/drivers/gpu/powervr/render.h`:

```c
int powervr_render_lookup(const struct gpu_image_descriptor *image, const struct drv_gpu_scanout_backing *backing, struct powervr_buffer **result);
const volatile uint8_t *powervr_buffer_view(const struct powervr_buffer *buffer);
uint64_t powervr_buffer_bytes(const struct powervr_buffer *buffer);
void powervr_buffer_release(struct powervr_buffer *buffer);
```

- `import_image` に来た `backing` を、render の node が export で渡した page の list と**同一性**（pointer）で照合し、image の全部の field（core が足す device_id を除く）が export の時と同じ物だけを retain する（Pi 4 の lookup と同じ形、名前は写していない）。合わなければ ENOTSUP で何も retain しない。
- 渡す backing は page_bytes 4096、flags `DRV_GPU_BACKING_CONTIGUOUS`、bytes は確保の大きさ（page 単位に丸める前）。
- `powervr_buffer_view` は uncached の kernel の view（render の target は uncached、§3.2 の表）。読みは揃った plain な load だけ（damage の矩形だけを copy）。参照を持つ間は有効。
- 使い終わったら `powervr_buffer_release`。quarantine 中に最後の参照が落ちた buffer は reset まで保持される。

## 確かめ（2026-10-19、worktree /home/awe/zedBSD-worktrees/p2、host）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| VF2 の kernel | `timeout 1800 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/ws269-p006-vf2 vmunix` | rc=0、`warning:`・`error:` 0 件（-Werror）。新しい 5 file を含む 13 file が compile され、vmunix に `powervr: render node ready ...`・`powervr: coherency (firmware writes): ...` の文字列 |
| riscv64-virt の kernel | `timeout 1800 make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/ws269-p006-virt vmunix` | rc=0、`warning:`・`error:` 0 件（powervr は build されない構成。変更の影響が無い事の確かめ） |
| amd64 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws269-p006-amd64 vmunix` | rc=0、`warning:`・`error:` 0 件（同上） |
| host 試験（p006） | `timeout 120 sh plan/ws269/tests/p006/run.sh build/ws269-p006-host` | `PASS 54 FAIL 0`。entry の bit（catalogue・directory・page、CC が立たない事、SLC bypass・read only、40 bit）、map と 3 段の walk、2 MiB・1 GiB の境を跨ぐ map、重なりの EEXIST と不変、引数の拒否 5 通り、unmap と再 map、途中の失敗で何も残らない事、fini で table と管理の memory が全部返る事、address の空間（guard、隙間の再利用、拒否、満杯）、heap の page の map（claim・take・give・満杯・境界） |
| host 試験（p005、追従） | `timeout 120 sh plan/ws269/tests/p005/run.sh build/ws269-p006-p005host build/ws269-p002/fw/rogue_36.50.54.182_v1.fw` | `PASS 99 FAIL 0` |
| host 試験（p004、追従） | `timeout 120 sh plan/ws269/tests/p004/run.sh build/ws269-p006-p004host` | PASS（0 failures） |
| host 試験（p007、追従） | `timeout 120 sh plan/ws269/tests/p007/run.sh build/ws269-p006-p007host` | `display-host-test: PASS` |
| 空白 | `git diff --check` | 問題なし |

未実施: QEMU（起動しない指示、QEMU に PowerVR は無い）。実機（下の文面）。render の ops（open・blob・share・submit・recovery）は kernel の中でしか動かず、host 試験は無い（実機の自己試験の行で確かめる）。recovery の各 op は実機でも自然には起きないので、自己試験は通常の道（empty job・fence・memory・close の cleanup）だけを通す。stop・isolate・reset の実機の確かめは、hang を起こせる job（p008 の compute）ができてから。

## 追加: WS277 の依頼 R3・R5（2026-10-20 P2、q1344 / q1344-i01）

[WS277 の設計 改訂 2](../../ws277/design.md) の §9「他 WS への依頼」の R3（B2・D8）と R5（M1）を p006 の追加として実装した。HAL の API は変えていない。

| 依頼 | 実装 |
| --- | --- |
| R3 | `GPU_BLOB_SHAREABLE` の blob は作成の時から常に SLC_BYPASS で GPU の MMU に写す（mappable でなくても）。`resource_make` に shareable の引数を足し、写像の flag は `powervr_gpu_buffer_flags(mappable, shareable)`（`gpu-mmu.c`、host で試せる）で決める。import した resource（他の session の export）も shareable として扱う（従来どおり bypass）。storage の resource（mappable）は変わらない |
| R5 | render の node に `blob_create_placed`（`render_blob_create_placed`）を足した。`render_blob_create` と共通の `blob_make` で確保し、確保した run を `powervr_gpu_run_fits` で検査する: `DMA32` は上限 0xffffffff（`max_dma_address` が 0 か大きい時）、`max_dma_address` は含む上限、`alignment` は base の揃い、`CONTIGUOUS` は常に満たす（1 本の連続の run）。満たさなければ buffer を返して ENOTSUP。`COHERENT` は ENOTSUP（CPU の view は uncached の alias だけで coherent の確保を持たない）。core の検査（`gpu.c:2697-2705`: 未知の flag・reserved・2 冪でない alignment は core が EINVAL、条件があって op が無ければ ENOTSUP）に合わせ、成功はすべての条件を満たす |

| 確かめ（worktree /home/awe/zedBSD-worktrees/p2） | コマンド | 結果 |
| --- | --- | --- |
| VF2 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/ws269-p006-vf2 vmunix`（log `build/q1344-vf2.log`） | rc=0、`warning:`・`error:` 0 件。`gpu-mmu.c`・`render.c` が compile された |
| riscv64-virt の kernel | 同上 `config-riscv64-virt.mk`、`build/ws269-p006-virt` | rc=0、`warning:`・`error:` 0 件 |
| amd64 の kernel | 同上 `config-amd64.mk`、`build/ws269-p006-amd64` | rc=0、`warning:`・`error:` 0 件 |
| host 試験（p006 を拡張） | `timeout 120 sh plan/ws269/tests/p006/run.sh build/ws269-p006-host` | `PASS 68 FAIL 0`（追加 14: SLC の選択 4 通り（private・mappable・shareable・両方）、run の検査 10 通り（DMA32 の内・境・跨ぎ、上限なし、揃い・不揃い・1、2 冪でない、空、wrap）） |

未実施: QEMU（起動しない指示）、実機。R3 の効果（GPU が書いた non-mappable な shareable の image で古い画が出ない事）は WS277 p006 の実機（U9）で確かめる。render の ops の中の分岐（placed の確保・解放の道）は host 試験が無く、実機で確かめる。

## 実機の試験の文面（今は頼まない。p005 の実機で `running, health check ok` が出た後）

- **WS269 p006（VisionFive 2、GPU の render の node）**: main の `config/ci/config-visionfive2.mk` で作った image を SD card で起動し、画面の boot の log で、p005 の行 `powervr: firmware 1.1 build 6976702 running, health check ok, ...` の後に続く 3 行を控える: (1) `powervr: coherency (firmware writes): <判定>, control <OK|INCONCLUSIVE>, <n>/8 rounds, <s> stale, <m> missing`、(2) `powervr: coherency (firmware reads of cached writes): <seen without a clean | seen after a clean only>`、(3) `powervr: render node ready (self-test ok: empty job, fence, memory)`。(3) の代わりに `powervr: render self-test <段> failed (<番号>)` か `powervr: render node not registered ...` が出たら、その行をそのまま控える。login の後に `ls /dev/gpu*` で `/dev/gpu0`（display）と `/dev/gpu1`（render）がある事も見る。

## 残件

- 実機の判定（上の文面、Q1 が一覧に載せる）。(1) の判定が NOT COHERENT なら設計どおり（非 coherent を既定のまま）。INCONCLUSIVE（writes missing）なら MIPS の道の道具が FW の版で使えない事が分かるので、FENCE の付いた job の UFO（client の CCB の UPDATE）に替えて流し直す（D1 の debug の範囲外、p006 の再試行）。
- stop・isolate・reset の実機の確かめ（hang を起こせる compute の job、p008 の後）。
- page ごとの backing（map を持たない大きな blob、Minor 8 の残り）を Future Work の候補に（Q1 に登録を依頼）。
- service は IRQ と tick で起きる。pending の無い時は 100 ms ごとに FWCCB を読む（負荷は小さいが、APM を使う時に見直す）。
- p007 の後半（P1）: display.c の `import_image` から `powervr_render_lookup` を使う。constraints に SHARED・FOREIGN を足すのは P1。
- 片付け（Q1 に依頼）: worktree /home/awe/zedBSD-worktrees/p2 の `build/q1344-vf2.log`・`build/q1344-virt.log`・`build/q1344-amd64.log`（q1344）。worktree /home/awe/zedBSD-worktrees/p2 の `build/ws269-p006-vf2/`・`build/ws269-p006-virt/`・`build/ws269-p006-amd64/`・`build/ws269-p006-host/`・`build/ws269-p006-p005host/`・`build/ws269-p006-p004host/`・`build/ws269-p006-p007host/`・`build/ws269-p006-vf2.log`・`build/ws269-p006-virt.log`・`build/ws269-p006-amd64.log`・`build/ws269-p006-rc.txt`。
- **誤って main の checkout に出した物（Q1 に片付けを依頼）**: 並行の build の shell の書き方の誤り（`cd … && (A) & (B) &` の B が元の cwd で動いた）で、amd64 の build が 1 回 `/home/awe/zedBSD-claude1` の中で走り、`/home/awe/zedBSD-claude1/build/ws269-p006-amd64/`・`/home/awe/zedBSD-claude1/build/ws269-p006-amd64.log`・`/home/awe/zedBSD-claude1/build/ws269-p006-rc.txt` を作った（main の source は変えていない、`git status` は clean のまま）。表の amd64 の結果は worktree で流し直した物。
