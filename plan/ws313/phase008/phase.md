<!-- awesome-plan project=zedbsd record=ws313-p008 -->

# ws313-p008: GPU（Mali-G310 の CSF、firmware、MMU、job）と Vulkan の device

Status: in-progress（実装済み・実機未試験。Vulkan の段は設計だけ）
Queue: q1675-i02（P7 の配下 B）
WS: [WS313](../ws.md)、設計 [gpu.md](../gpu.md)

## 目的

Mali-G310（Valhall v10、CSF）を起こし、firmware を読んで MCU を動かし、global の interface と driver の command stream を起動する。
Vulkan の段（render の node・executor、Valhall の shader compiler、CS の命令の builder）は設計と見積りまで（[gpu.md](../gpu.md) §11）。

## 実装（2026-10-29）

- `mali-attach.{h,c}`: `int drv_mali_gpu_attach(uint64_t fdt_phys);`（0 か負の errno）。FDT（`arm,mali-valhall-csf`・`arm,mali-midgard`）の reg・interrupts、glue の power up、map、power override、ID の 1 行、arch 10 の確かめ、割り込み、soft reset、L2、後の段の kthread（firmware の読み、load、MCU の起動、PING、queue、1 s ごとの報告と 10 s ごとの PING）。
- `mali-s7d.c`: S7D の sys_mali の gate、MALI・MALI_STACK の clock（fclk_div5、400 MHz）、RESET_MALI・MALI_CAPB3 の release、PWR_KEY・PWR_OVERRIDE1。
- `mali-mmu.c`: buffer（4 GiB より下、uncached の view）、AS の init・map・enable・disable・flush、MCU と group の空間の bump の割当。
- `mali-firmware-file.c`: 根の file system を 90 s まで待って読む。
- `mali-interface.{c,h}`: firmware の interface の offset（Linux v7.3 panthor_fw.h の並びの値）、reader と検べ、req の toggle・update・ack、timer の変換。
- `mali-csf.c`: section の load と map、MCU_CONTROL = AUTO と起動の poll、interface の読み、global の設定、PING、doorbell、status の報告。
- `mali-queue.c`: CSG 0・CS 0 を AS 1 の 64 KiB の空の ring で起動（doorbell 1）。
- `platform/arm64/mali.mk`: `ARM64_MALI_SOURCES`。
- 2026-10-29（q1675-i03）: render の node（gpu1）は root の mount の後の firmware の読みの後に出るので、`drv_mali_gpu_attach` が後の段の thread の前に `kern_gpu_attach_begin()`、後の段が node を出した（か諦めた）所で `kern_gpu_attach_end()`（powervr・amdrdna・nvrtx・i915 と同じ形）。sessiond は hw.gpu.attaching が 0 になるまで desktop の起動を待つので、compositor の側は変えない。thread を作れなければその場で end。
- driver の arch minor の file が無ければ arch10.12・10.10・10.8 を順に読む（linux-firmware の 3 本は同じ file、[gpu.md](../gpu.md) §4）。

## 確かめ

- `make ZEDBSD_CONFIG=config/ci/config-odroid-c5-keiland.mk BUILD=build/pb build/pb/vmunix`: exit 0、warning 0（2026-10-29、配下 B）。text の config（MALI=n）は P7 が確かめる（未実施）。
- host: `tests/mali-pagetable-host-test.c`・`tests/mali-firmware-host-test.c`・`tests/mali-interface-host-test.c` の 3 本が `all checks passed`。
- 実 firmware（linux-firmware の arch10.8/10.10/10.12、同じ SHA-256）を driver の parser で読めることを host で確かめた（section 7、protected 1 を skip、共有の section 0x04000000〜0x04010000）。
- 実機: 未実施。QEMU: 未実施（S7D・Mali の machine が無い）。

## 夜の試験で見る物

- boot の step の log に `a GPU device began attaching` と、`mali: render node ready …`（か失敗の行）の後の `a GPU device finished attaching`。desktop は後者の後に始まる（sessiond の待ちの上限まで）。

[gpu.md](../gpu.md) §9 の「夜の serial で見る行」の 2〜10（clock の値、GPU_ID と arch、reset と L2、firmware の file、interface の版、PING、queue、job の割り込み）。

## 再開点

- 実機の結果で直す（GPU_ID が読めない → clock・reset・power の値、MCU が起きない → MCU の status・fault の行）。
- 次の段は [gpu.md](../gpu.md) §11 の G8 の最小 → G6 → G7。
