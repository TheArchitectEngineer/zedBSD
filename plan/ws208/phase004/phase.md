<!-- awesome-plan project=zedbsd record=ws208-p004 -->

# ws208-p004: 負荷と driver の競合（QEMU）

Phase ID: `ws208-p004`
Parent: [WS208](../ws.md)
Status: in-progress（source の洗い出しと 1 件の直し。QEMU の負荷は T1-537 の結果の後）
Phase disposition: normal
Queue: q939（P2、2026-10-11）
依存: [p003](../phase003/phase.md)

## source の洗い出し（2026-10-11 P2）

Pi 4 で使う driver のうち、「割り込みを止めれば排他」（UP の前提）で共有の状態を守る所を探した。

| 所 | 結果 |
| --- | --- |
| `src/drivers/platform/rpi4/rpi4-firmware.c`（mailbox） | **直した**（c34497c3e）。要求の buffer の `busy` を irq を止めて読み書きしていたので、別の CPU の 2 つの要求が同じ buffer に書けた。atomic の test-and-set にし、取られていれば polling の間隔で待つ（上限は mailbox の待ちの 2 倍、超えたら EBUSY と log） |
| `rpi4-sdhci.c` | mutex（SMP で可） |
| `rpi4-ethernet.c`（GENET）・`bcm54213pe.c` | `device->lock` の spin lock（可） |
| `pci-brcmstb.c`（PCIe の config） | `config_lock` の spin lock（可） |
| `pci.c`・`usb.c`・`pci-xhci.c` | irq を止める所は全て atomic・lock と組み（amd64 で SMP 済み） |
| `rpi4-console.c`、HAL の `bsp-rpi4/cons.c`、`kern/klog.c` | console の出力は lock 済み |
| `src/drivers/gpu/bcm2711/`（WS141） | irq を止めて排他する所は無い。SMP の試験で競合が出たら D5 で WS141 の session へ |

## 確かめ

- rpi4 の vmunix の named build rc 0・warning 0（c34497c3e）。
- QEMU の負荷: 未実施（T1-537 の (b) の結果を見て、足りなければ追加を依頼）。

## main の統合（2026-10-13 P6）

ユーザー（2026-10-13）「これ以上詰めなくていいです。SMPに進みます。」→ WS208 を仕上げる。

### merge（agent/p2、663996224 と b44b2420d、ともに WIP）

`git merge main`（main b72242804、続けて bbb426cd5）。衝突は 3 つ:

- `plan/ws208/ws.md`: この branch の Phase の表と main の「全部承認」「SMP は最後」の節を両方残した。
- `src/hal/arm64/page.c`: この branch の `pmem_lock` と、main の allocator（`next_free`・`next_occupied`・`claim_run` の語単位の走査、`first_free_candidate`）を両方。`hal_pmem_alloc`・`_limited`・`_free`・`hal_get_memstat` が `pmem_lock` の下で `alloc_ram`・`free_ram` を呼ぶので、main の cursor と bitmap も lock で守られる（内側の `arm64_irq_save` は lock が既に IRQ を止めているので重複だが無害）。
- `src/hal/arm64/space.c` の `hal_pmem_unmap_uncached`: main の「穴を先に調べ、連続の run ごとに cache を捨て、最後に TLB を 1 回 flush」を採り、IRQ を止めるだけだった所を `kernel_table_lock` に。穴で返す経路は何も外していないので flush は要らない。`arm64_flush_tlb()` は `tlbi vmalle1is`（broadcast）。

### main の変更が SMP で成り立つか（source を読んだ確かめ）

| main の変更 | 確かめ |
| --- | --- |
| `src/kern/lock.c` の mutex（K2-1、`hal_cpu_count() <= 1` で spin しない） | arm64 の `hal_cpu_count()` は `arm64_smp_configure()` の数（`cpus=1` なら 1）。4 CPU では `MUTEX_SPIN_LIMIT`（256）回だけ見て眠る（amd64 と同じ）。secondary が online になる前の数分の spin は無駄だが無害。 |
| `sched.c` の K-D（kern.perf=1）の計数 | 窓は CPU ごとの `sched_cpu` にあり CPU の lock の下、報告は CAS で 1 thread だけ、集める時は各 CPU の lock を取る。arm64 の counter（CNTVCT）は全 core で共通。 |
| copyin・copyout の pin（`uaccess.c`・`vmspace.c`） | 他の CPU の munmap と競合する場面を pin が防ぐ（SMP でこそ要る物）。page table の変更の TLB は broadcast（D2）。 |
| bcm2711 の display（session の gate 外し、`table_lock`、overlay） | `table_lock` と display の `flip.guard` は全て `spin_lock_irqsave`。割り込みは全て CPU 0（D1）なので handler は CPU 0 だけ、他の CPU は irqsave で取る。IRQ を止めるだけの排他は bcm2711・rpi4 の driver に無い（grep）。 |
| `rpi4-audio.c`（新しい driver） | `audio->lock` を `spin_lock_irqsave`、割り込みは CPU 0。 |
| HAL の `cons.c`（serial 既定 off、`output_suspended` の atomic） | 出力は `output_lock` の下。早い return は atomic の読み。 |
| HAL の `int.c`（alignment fault の分類） | CPU の状態を持たない。 |

### 確かめ（2026-10-13 P6、b44b2420d）

- rpi4 の vmunix: `make -j8 -o $PWD/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix` rc 0・warning 0、`arm64 vmunix check: PASS`、cksum 3361400286 8200192、SHA256 a9d6c6d0…2492（`build/ws208-pi/vmunix` に複写）。
- amd64 の vmunix: `make -j8 -o …/build/amd64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/vmunix` rc 0・warning 0。
- host: `plan/ws213/tests/lock-host-test.sh`・`pin-host-test.sh`・`kd-host-test.sh`、`plan/ws141/tests/display-host-test.sh`、`plan/ws214/tests/display-p003a-host.sh` が全て PASS。`tests/rpi4-fdt-host-test.c` PASS（4 CPUs）。
- QEMU（T1）・実機（p005）: 未実施。

### main に入れてよい形か

- **1 CPU（`cpus=1`）の振る舞い**: `hal_cpu_start_others()` は何もしない。UP の時から変わる所は、競合しない spin lock（IRQ を止めて取る。以前の「IRQ を止める」と同じ）、`TPIDR_EL1` の CPU の record、space の切り替えの TLB の flush が自分の CPU だけ（1 CPU では同じ）、EL2 で `VPIDR_EL2`・`VMPIDR_EL2` に本物の値、mailbox の `busy` の atomic。機能は変わらない見込みで、QEMU では T1-537 (d) が login まで起動。実機の `cpus=1` は未確認。
- **既定**: D3（ユーザー承認）どおり既定で DT の全 core（Pi 4 は 4）を起こし、`cpus=N` で減らす。`cpus=` は firmware が `cmdline.txt` を `/chosen/bootargs` に入れた物を語の頭で探すので、FAT の `cmdline.txt` の 1 行目に空白で足す（`login=graphical cpus=1`。行を増やさない）。
- **勧め**: Pi の実機（p005 の起動・desktop・network・SSH・reboot）を branch の vmunix で先に確かめてから main に入れる。日程の都合で先に入れる時は、Pi の失敗に備えて `cpus=1` で今の UP に戻せることを release の注記に書く（既定の `cmdline.txt` に `cpus=1` を入れる案は D3 と違うのでユーザーの判断）。

### T1 への依頼（QEMU raspi4b、Q1 経由）

image は agent/p2 の b44b2420d 以降を `config/ci/config-rpi4.mk` で build。QEMU raspi4b は 4 core 固定（`-smp` 不要）。
1. 既定: login の PNG。dmesg に `boot: HAL initialized successfully. [cpu 4` と `boot: CPUs ready: 4`（`ARM64 SMP …` の行は HAL の console だけなので dmesg には出ない）。
2. 4 本の `sh -c 'i=0; while [ $i -lt 300000 ]; do i=$((i+1)); done' &` を wait、その間 `ps`・`top -b -n 1`。4 本の TIME がほぼ同じに進むこと。
3. 4 core のまま `reboot` → login に戻る（PNG）。
4. `-append "login=graphical cpus=1"`: login の PNG、`CPUs ready: 1`。
固まる・起動しない時は FAIL とし、`-S -gdb tcp::<port>` で起こし直して `info threads`、各 thread の `x/i $pc`・`bt` を記録（symbol は `build/arm64/kernel.elf`）。

### Pi の実機（p005、Q1 が束を置いてユーザーへ）

- vmunix: `/home/awe/zedBSD-worktrees/p6/build/ws208-pi/vmunix`（b44b2420d、cksum 3361400286 8200192）。FAT の boot partition の `vmunix` を置き換える（`config.txt` の `kernel=vmunix`、armstub は firmware の既定）。
- 見る点: `dmesg | grep -E 'CPUs ready|HAL initialized'` が `CPUs ready: 4`・`[cpu 4`、desktop が出る、network（ping）と host からの SSH、4 本の busy loop で `top` の %Cpu が 1 core 分を超える、reboot 10 回すべてで desktop と SSH まで戻る。
- secondary が来ない時は画面に `ARM64 SMP cpu N (affinity …) did not come up; …` と出て止まる（D6）。固まる時は画面の写真。
- 1 core に戻す: FAT の `cmdline.txt` の 1 行目の末尾に空白と `cpus=1`（例 `login=graphical cpus=1`、行は増やさない）。`cpus=2`・`3` も使える。
