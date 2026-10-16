<!-- awesome-plan project=zedbsd record=ws039-license-audit -->
# amdrdna の参照の正本と license の監査（ws039-p001）

作成: 2026-10-15、P3（q1202）。この文書は出典の code・comment・symbol の名前を含まない（path・hash・license の判定と、取ったデータの種類だけ）。規則は [Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」の WS039 の項と [design.md](design.md) の 13 節。

## 正本

| 正本 | 版 | 取得元（`plan/ws039/temp/`、commit しない） | 用途 |
| --- | --- | --- | --- |
| Linux（amdgpu・DC・swsmu） | tag `v6.19`（commit `05f7e89ab9731565d8a62e3b5d1ec206485eeb0b`、nvrtx と同じ） | `https://github.com/torvalds/linux`（sparse: `drivers/gpu/drm/amd/amdgpu/`・`include/`・`pm/swsmu/{smu11,inc,*.c}`・`display/dc/`・`display/include/`・`display/amdgpu_dm/`（2026-10-15 に追加）、`include/uapi/drm/amdgpu_drm.h`） | register の offset・bit、IP discovery の表、firmware の container、PSP・SMU の command と手順、GMC・IH・CP・SDMA・DCN の手順と値 |
| Mesa（RADV・ACO・common・registers） | tag `mesa-25.3.6`（commit `06f9e28304d5d3f109c33535c1c25b9df5769af2`） | `https://gitlab.freedesktop.org/mesa/mesa.git`（sparse: `src/amd/`・`docs/license.rst`・`include/drm-uapi/amdgpu_drm.h`） | 描画の state の register と値、記述子の形、user SGPR の約束、cache の flush、chip の識別。compiler（p007）の出典は Mesa 25.0.7 の tarball（P5 の表、[phase007](phase007/phase.md)） |
| linux-firmware | tag `20260410`（commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120`、AX211・i915・nvidia-gsp と同じ） | `https://gitlab.com/kernel-firmware/linux-firmware.git`（sparse: `amdgpu/navi1*`・`WHENCE`・`LICENSE.amdgpu`） | Navi10 の firmware（BLOB） |
| libdrm の `amdgpu.ids`、`pci.ids` | host の sysroot と `/usr/share/misc/pci.ids`（2025.06.09） | 読むだけ | RX 5600 XT の revision、RDNA3 の device id |

判定の方法: 各 file の先頭 6000 byte の SPDX の行、または MIT の permission notice の機械判定（`plan/ws039/temp/lic.py`、commit しない。amdgpu_dm の 3 file は 2026-10-15 に同じ規則で判定）。先頭だけの判定は file の途中に埋め込まれた別の license を見逃すので、p010 で参照した file の全文を確かめる。

## 判定の要点

1. **Linux の amd の取得範囲はほぼ MIT**（2191 file のうち MIT 2182。GPL-2.0-only 1、GPL-2.0+ 2、GPL-2.0 OR MIT 5、表記なし 1）。下の表で参照した file のうち MIT でないのは `amdgpu_vm_pt.c`（GPL-2.0 OR MIT）だけ。今の規則では license に依らず**データ（値・表）と手順の参考だけ**を使い、code・comment・構造・symbol の名前は写さないので、GPL の file でも扱いは同じ（Guardrail、2026-10-15 ユーザー「GPLなコードを引用しても、引用ならライセンスは影響しない」）。
2. **Mesa の `src/amd` は MIT 563・表記なし 128**。表記なしの物（`registers/*.json`・`docs/license.rst`）は Mesa の既定（`docs/license.rst`: 大部分 MIT、file ごとに SPDX を見よ）に従う。
3. **BLOB**: Navi10 の firmware は AMD の `LICENSE.amdgpu`（SHA-256 `572872598565dc3513470de971a32bf9db301f47afeef3636345eadae33b2eee`。要点: binary の形の再配布可、notice の再掲が条件、reverse engineering・decompile・disassemble の禁止。`WHENCE` の SHA-256 `c282239a5a2d849677e9304e6f361e475e1b6e71e7c771c03f8986f71b309527`）。base の system・kernel・repository の source には入れず、`userland/firmware/` の既定 off の package（design の 4.1）にし、driver が file から load する。driver は MIT の header（`amdgpu_ucode.h`）が定める container の欄だけを読み、payload を解析しない。source の中に firmware に当たる byte の配列は、取得範囲では golden の register の表（データ）だけで、BLOB は無い（p010 で再確認）。
4. VBIOS（AtomBIOS）は A1v で data の表の header と欄を読む（読むだけ。改訂 2 から。p002 の実装）。image は repository に入れない（試験は実機から取り出した image を `plan/ws039/temp/` に置いて使う）。
5. **p007（compiler）の出典は Mesa 25.0.7**（P5 の tarball、SHA-256 は [phase007](phase007/phase.md) の表。下の Mesa 25.3.6 の表の同名の file は driver の側の参照で、p007 の block の comment と P5 の `amdrdna-compiler-license-audit.md` は 25.0.7 の版と SHA-256 を使う。review M9）。
6. **zedBSD の code は Zlib**。出典の copyright・MIT の表示は付けない（データの引用なので）。file の先頭に出典を書かず、block ごとに「Value(s) referenced:」「Procedure referenced:」を書く（design の 13.2）。

## file ごとの表（作業の文書と design.md が参照した file）

「取るデータ」の欄: 値 = register の offset・bit・enum・packet の形・表、手順 = hardware の操作の順（Procedure referenced の対象）、読むだけ = 設計の判断の根拠で code に値を取らない。

### Linux v6.19（commit `05f7e89ab9731565d8a62e3b5d1ec206485eeb0b`）

| path | SHA-256 | license | 取るデータ |
| --- | --- | --- | --- |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_drv.c` | `9ca9e5fcd4e1c0cddfdad92d07f14a86a519a0cfc1878ac3cab4fb7158c57e86` | MIT | 値（device id の表）、読むだけ（module の既定） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_device.c` | `99b104bb16c37299ad00e767daf278f03c9a2145782237b2c1e1e3122a121e5d` | MIT | 手順（MM_INDEX の VRAM 読み、HDP の flush の規則、reset）、読むだけ |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c` | `480aa5509daa456d49f8423b46e48c6b574b513cd9d9b70879bb74cb4b179f7d` | MIT | 手順（discovery の表の読みと検査）、値（register の dword） |
| `drivers/gpu/drm/amd/include/discovery.h` | `bd64513b3397e3b2633a0a3e45e95a546b26dd5789f30d1a902a8b415528c8d3` | MIT | 値（表の layout・署名・id） |
| `drivers/gpu/drm/amd/include/soc15_hw_ip.h` | `34a04ab0e1411f4833f2d2676ffd4c5b4b301b9bdef9d1f242adb89105bc733f` | MIT | 値（HWID） |
| `drivers/gpu/drm/amd/include/navi10_ip_offset.h` | `d9488ceab9b664db8a53b0bf6efcfe355e43a4c953100a7c53da705b5fcc1d03` | MIT | 値（IP の base、実機の discovery と照らす） |
| `drivers/gpu/drm/amd/amdgpu/soc15_common.h` | `2ee34db532a920980856dd390b2bb1a336791299cab7e9ab160c6cfec8ae27cd` | MIT | 読むだけ（register の addressing の規則） |
| `drivers/gpu/drm/amd/amdgpu/nv.c` | `fdff09b468cf5c99a2a24f87a966ab274cb874d7ea14ca710732080a5870b0c6` | MIT | 手順（reset、doorbell の index、GRBM の select）、値（external rev） |
| `drivers/gpu/drm/amd/amdgpu/nbio_v2_3.c` | `295edda60b69ad54056ecf95d5fe4352d1d79d6f1493cbc6f067b09a983c7e39` | MIT | 手順（HDP の flush、doorbell の aperture・範囲、IH の制御）、値（flush の bit） |
| `drivers/gpu/drm/amd/include/asic_reg/nbio/nbio_2_3_offset.h` | `20157d63ad73607543c62b63245671dd76658c31acfa1036ab01efe96cdb6e1c` | MIT | 値（NBIO の register） |
| `drivers/gpu/drm/amd/include/asic_reg/nbio/nbio_2_3_sh_mask.h` | `1a7ba6c5f821be95fe0ba4396f2c366ddc072b2e949b91154e48b706cc5dc0ba` | MIT | 値（NBIO の field） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_atombios.c` | `7a524f0728e66c763ce48d8ae84eb9dbe14147da973abfb16d582fb13feb5c9b` | MIT | 値（post 済みの bit） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_atomfirmware.c` | `9a477e36ffdc25eb204a2175b523eb6de38d11532dcaa32ccf76da717b8a15c6` | MIT | 読むだけ（scratch の base） |
| `drivers/gpu/drm/amd/include/atombios.h` | `2f543d5518d0a2bfd3a20c0220faac6edf1f1023279c952d503edffc02667dc0` | MIT | 値（scratch の bit） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_doorbell.h` | `5be18a6bce42879b959a05b19ed8904190f0105eb5bb5e3637a3138ec73903ae` | MIT | 値（doorbell の index） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_doorbell_mgr.c` | `bcb34ec381f6d44b85260954d359cde526d6bf513854862eac66b5b565cc6bc6` | MIT | 読むだけ（BAR2・単位） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_ucode.c` | `c28085638023bc9bf40eb21ddd94b06e62e935f87d948810e80cc8c4b2858b46` | MIT | 手順（image の切り分け）、値（IP の版と firmware の名前） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_ucode.h` | `9b717be2e98470203d188c44c6a9468e37efbec397140af9f4106518b6f31e3c` | MIT | 値（container の header の layout） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_psp.c` | `2ce397dd348eecf288fff8080d138c4a1de4fba02775217289dfc98fced04469` | MIT | 手順（command の投入、TMR、load の順）、値（buffer の大きさ・整列） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_psp.h` | `1aadf90806d38173241f825aa8908c20cc77396af88d1c3f24ef6d784bebb114` | MIT | 値（bootloader の command、buffer の定数） |
| `drivers/gpu/drm/amd/amdgpu/psp_v11_0.c` | `0ed98a02c128753bfbfda5ff33b3a03c98511684f0a58bcaabaf3dbd0c468327` | MIT | 手順（bootloader・ring・mode1 reset・停止）、値（待ちの mask） |
| `drivers/gpu/drm/amd/amdgpu/psp_gfx_if.h` | `0cc6b4f6c4112ab3e5e052b412bcfbcde25e4125ad34b27f26b140d2b2d770fa` | MIT | 値（command の id・payload・frame・応答の layout、fw type） |
| `drivers/gpu/drm/amd/include/asic_reg/mp/mp_11_0_offset.h` | `085538cc9ae20027564682395fd3958dab2553fdf739c101d294f42fd4b63df7` | MIT | 値（MP0・MP1 の register） |
| `drivers/gpu/drm/amd/include/asic_reg/mp/mp_11_0_sh_mask.h` | `9f6e5f556511d3b1bf3d27595fbb3a8416a88170af3aa7fbcf56627d67cd7a5c` | MIT | 値（FIRMWARE_FLAGS の bit） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_rlc.c` | `32a5c62305d6e45b01c9ebf0df6bad2fb2b68eafe9764365aaf077cf341267fe` | MIT | 値（RLC の list の切り分け） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_sdma.c` | `b40cfd54a3771837f80e6f9f352a2739e79ff2f3478b78ced61a3c26c71386f2` | MIT | 読むだけ（SDMA の image の扱い） |
| `drivers/gpu/drm/amd/pm/swsmu/amdgpu_smu.c` | `01e1930e8f4207c9c73e71c76440804f8080602e2094bae49144723ac2c471b6` | MIT | 手順（SMU の起動、Tier 0 の範囲） |
| `drivers/gpu/drm/amd/pm/swsmu/smu_cmn.c` | `c955057deba7e72e4db6556740f8a8e5e276a6e6831897161f1439f78ae9975a` | MIT | 手順（mailbox）、値（応答の code） |
| `drivers/gpu/drm/amd/pm/swsmu/smu11/smu_v11_0.c` | `7b0863ab5eea9a6fbdbd630a5c4ee93fb36d4dd4036fad0f3b7213c155047ce6` | MIT | 手順（mailbox の register、版の確認）、値（register） |
| `drivers/gpu/drm/amd/pm/swsmu/smu11/navi10_ppt.c` | `dac80744719eb2a6f424475ce845d5418646f51e67c8064c6354fbc9d864dfe0` | MIT | 値（message の id の表、GFXOFF の条件） |
| `drivers/gpu/drm/amd/pm/swsmu/inc/smu_v11_0.h` | `386cbbda3978be7441559ec32549ec109f543f3efd0c279dc74a7045daa865c6` | MIT | 値（driver-if の版、FIRMWARE_FLAGS の address） |
| `drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu_v11_0_ppsmc.h` | `b0901c743de3ff44ec42d7321a7715d52f901e7a45f3f96ba0e1e2e73b204ae9` | MIT | 値（message の番号） |
| `drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu11_driver_if_navi10.h` | `44edd47e4e9fd94cd5dc62c4de754f586507b90beceb9cb860a9983466b5191f` | MIT | 値（feature の bit、table の id） |
| `drivers/gpu/drm/amd/include/amd_shared.h` | `20c48a1c79fb48a3d41c11b204b75594970637a92834ff292c9877a0ecce9b95` | MIT | 値（APU の flag、GFXOFF の mask） |
| `drivers/gpu/drm/amd/amdgpu/gmc_v10_0.c` | `2e4eb8564f4eb6202c6892c6b4d6a992199fb5e093d6c41160b473265f8ce478` | MIT | 手順（GART の enable、TLB の invalidate、fault）、値（PTE の bit、VM の大きさ） |
| `drivers/gpu/drm/amd/amdgpu/gfxhub_v2_0.c` | `762d171e2d274af607ac92bc47ae85d8d7d805705a4e8d22e2090a033893c78c` | MIT | 手順（VMID 0・context の設定）、値（invalidate の req） |
| `drivers/gpu/drm/amd/amdgpu/mmhub_v2_0.c` | `5249f7d87b90fd1e2dc9ae52c51842400e5f8bc23d3a88f491f446b7ddf33053` | MIT | 手順（同、MMHUB） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_gmc.c` | `05574d30b9a6803706fdfff247a8746e2a10435f46a2cc68a8d50b9d932c27ff` | MIT | 手順（GART の配置、entry の書き方、engine の割り当て、SDMA の errata） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_gmc.h` | `5f52b08073bc4b126e79a76760861f67a37951c65cd48bb01163dcb342ee69a0` | MIT | 値（fault の flag） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_gart.c` | `5894b6bcd7af18f81c5474364f5280ee63fe97bf305b0d69956076ef2fda4901` | MIT | 手順（表の確保と更新） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_vm.c` | `3d485e5095560795e6ea9d554c74286f7ebd8113bfb6bd254872b916dc963e7c` | MIT | 読むだけ（VM の大きさの計算） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_vm.h` | `ce197aa8d469cb64c6abecce7bc5d16e64fa0a2c76fe9286af5ab1306eeef86a` | MIT | 値（PTE・PDE の bit） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_vm_pt.c` | `817e09df3d56455ea255a211b5cd2d9a3924e4a8a44df21b71e549e39c0a867e` | GPL-2.0 OR MIT | 読むだけ（段の shift、huge page） |
| `drivers/gpu/drm/amd/amdgpu/hdp_v5_0.c` | `60dffbbf78b43bd1fabed6630d900af78e0db32a596369ac0fc557266115132b` | MIT | 手順（HDP の flush・invalidate・init） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_hdp.c` | `5c1f4f1ddeebbffd3833d0668a390f45de2be0211c5226cfbe1bd7774ccc0ca5` | MIT | 手順（CPU の flush） |
| `drivers/gpu/drm/amd/include/asic_reg/gc/gc_10_1_0_offset.h` | `a0ad8bffb3c07ec3824d306aff49c77bc40057f8760aec7a76ee58a60cc8e6a2` | MIT | 値（GC の register） |
| `drivers/gpu/drm/amd/include/asic_reg/gc/gc_10_1_0_sh_mask.h` | `cba20414ecf02e91bafdcfbff7d4ba686326f0bdaf236d1a883364f08410c167` | MIT | 値（GC の field） |
| `drivers/gpu/drm/amd/include/asic_reg/gc/gc_10_1_0_default.h` | `71591e59bdc749504017499be70483851534d8c17c76a724f48fdf1ecd0b9124` | MIT | 値（L2 CNTL の既定） |
| `drivers/gpu/drm/amd/include/asic_reg/mmhub/mmhub_2_0_0_offset.h` | `8160f69caa71331f305d8b8fc3f82b4c1c213e48a81c529efce47c4c6ea0c291` | MIT | 値（MMHUB の register） |
| `drivers/gpu/drm/amd/include/asic_reg/mmhub/mmhub_2_0_0_sh_mask.h` | `90c66fa3730d8450bcdacdaef8ed1d8f525957ce1a75ba5e552f92fef206cdee` | MIT | 値（MMHUB の field） |
| `drivers/gpu/drm/amd/include/asic_reg/hdp/hdp_5_0_0_offset.h` | `ad22bc29e81be169bd6c775311c6087ae89dcc148e69c47ef854496a71204c51` | MIT | 値（HDP の register） |
| `drivers/gpu/drm/amd/include/asic_reg/hdp/hdp_5_0_0_sh_mask.h` | `79591bdf610c254f6906d900ae8f815b6b4248a2e93f3ff8ec9fe8a44befc03c` | MIT | 値（HDP の field） |
| `drivers/gpu/drm/amd/include/navi10_enum.h` | `26bf943bd92a5282814385cf80f3c7ced99ba0c534ae1b932d1bf4293fa2feed` | MIT | 値（MTYPE、event、SH_MEM の enum） |
| `drivers/gpu/drm/amd/amdgpu/navi10_ih.c` | `465cdd203c48d2b35d8ac954354be24eee2b366b96fd11cb90607a1e93ff8563` | MIT | 手順（IH の ring の設定・処理） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_ih.c` | `680b3a963673e3fd8de3e48e30d4374489df9edab0c11fe9905df8ffdab9dc93` | MIT | 値（IV の layout、ring の大きさ） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_irq.c` | `0a6c4af2cdba4b767eeeef42c21f93b22bcb5772e6a69c8029e31c008d7258f0` | MIT | 読むだけ（MSI、dispatch） |
| `drivers/gpu/drm/amd/include/asic_reg/oss/osssys_5_0_0_offset.h` | `cfb1f3e0d207e354bb232ff75ecaaaf7002c2ccc2e18520abb6dbfa18373e9ef` | MIT | 値（IH の register） |
| `drivers/gpu/drm/amd/include/asic_reg/oss/osssys_5_0_0_sh_mask.h` | `7de95f6de08d14cae300ecc3614de639cfff3a08623db1e6841c340423a9b7f5` | MIT | 値（IH の field） |
| `drivers/gpu/drm/amd/include/soc15_ih_clientid.h` | `2dd2345bebcdaffb055620ba05dbdb577b26b694fce34744b4e3319eec236575` | MIT | 値（client id） |
| `drivers/gpu/drm/amd/include/ivsrcid/gfx/irqsrcs_gfx_10_1.h` | `940eda0af423d130acb295e2262a3225e766bab994e167619b7238e9cd3bac05` | MIT | 値（src id） |
| `drivers/gpu/drm/amd/include/ivsrcid/sdma0/irqsrcs_sdma0_5_0.h` | `5f2729a1f2a65e9d58f7f206c084b1db09d4cd7620f5553429d212a118a446a3` | MIT | 値（src id） |
| `drivers/gpu/drm/amd/include/ivsrcid/sdma1/irqsrcs_sdma1_5_0.h` | `21e5d44b4482728132b78f32af799abdb4b898580a19d95b79ae40b9b6957ea9` | MIT | 値（src id） |
| `drivers/gpu/drm/amd/include/ivsrcid/vmc/irqsrcs_vmc_1_0.h` | `c17236e95a8db469e50bfd4210ab5ef4afc8f838892babe60c26b0f5671e180e` | MIT | 値（src id） |
| `drivers/gpu/drm/amd/include/ivsrcid/dcn/irqsrcs_dcn_1_0.h` | `33280f2e465708a3aefe4454fc145b96c4178a03e237d2549efe09cdb10ee7e9` | MIT | 値（src id） |
| `drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c` | `6815899920a8c1bd4f9600bbc364268bc5a95f4fc1981522248dd7336f159bbe` | MIT | 手順（golden、constants、RLC の待ち、CSB、ring・HQD の設定、fence・IB・flush の packet、停止）、値（golden の表、MQD の値） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_gfx.c` | `c05c4f4d0f9efcfa62825c6341d5d8ce2950145fcc4e265ceccb8384da249102` | MIT | 手順（KIQ の packet、queue の番号）、値（CSB の中身） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_ring.c` | `16ca6516f5cf94eaa2c2fc7a62a337a1e923595256c15f2214a784ccc75cabb5` | MIT | 読むだけ（ring の大きさ・commit） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_fence.c` | `b88e903f4c70fb8555077f6f823a9ab29db9df1c28cd2b7bf11be320f53c32db` | MIT | 手順（fence の比較） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_ib.c` | `37ba33e2e63a28fe635a04174cf2cb9021e609516accc5c600bdacb7f0b0daff` | MIT | 手順（IB の前後の flush） |
| `drivers/gpu/drm/amd/amdgpu/nvd.h` | `0a4de0b61f8be33d83e7dc1af4b96370898af36d211f296daf018e72820ea0eb` | MIT | 値（PM4 の opcode・field・SET_*_REG の base） |
| `drivers/gpu/drm/amd/amdgpu/clearstate_gfx10.h` | `e6ca3544dd51a657d9200243be9e6d05c4dcfa052a239dadebbc6cd7409e64cd` | MIT | 値（clear state の表） |
| `drivers/gpu/drm/amd/amdgpu/sdma_v5_0.c` | `98ea1d8ac1d56d5fbbd44daafa37b63f935eceb962ed2c6a930e708546b70ef6` | MIT | 手順（SDMA の起動・停止、packet の組み立て）、値（golden の表） |
| `drivers/gpu/drm/amd/amdgpu/navi10_sdma_pkt_open.h` | `ff093a7c887625f20b21c2e64ee1f464d942c6da36e145cf69e1e9f530d3e01d` | MIT | 値（SDMA の packet の field） |
| `drivers/gpu/drm/amd/amdgpu/sdma_common.h` | `1e9d9423508df288bb3804fb42bc060605445dd14d31a5ea9ac5df7e6ce7c80b` | MIT | 値（cache policy） |
| `drivers/gpu/drm/amd/include/v10_structs.h` | `907f1822aae243e49cf1565c46262e0b6064bed678553d497250382759a90059` | MIT | 値（MQD の layout） |
| `drivers/gpu/drm/amd/display/dc/resource/dcn20/dcn20_resource.c` | `b31b4bb591ae2f34b8d5874580c4bb5737911e569c167e9f5c9f5044b738469e` | MIT | 値（instance の数、register の addressing） |
| `drivers/gpu/drm/amd/display/dc/hubp/dcn20/dcn20_hubp.c` | `03ed6b980a61fd3bc49a8e42de263069b71c9f8b2530255861aa4103139745f6` | MIT | 手順（flip、cursor、format）、値（format の code） |
| `drivers/gpu/drm/amd/display/dc/hubp/dcn10/dcn10_hubp.c` | `1d9206b942fd4792ad37bd0ba24219be2cb81b79aaece5ea0f66efbaac05c982` | MIT | 手順（viewport、blank、cursor の pitch） |
| `drivers/gpu/drm/amd/display/dc/optc/dcn10/dcn10_optc.c` | `0555e7f3d0fbd75c2fa3f70517de02b01e264dbdd61bddfc0521c3ff61c47846` | MIT | 手順（timing の読み、lock、blank）、値（timing の encode） |
| `drivers/gpu/drm/amd/display/dc/optc/dcn20/dcn20_optc.c` | `0e4f24cc13bca54608e3eceabb2bc09431985ed7e9f552919e1491a6b7040dea` | MIT | 手順（OPP の source） |
| `drivers/gpu/drm/amd/display/dc/mpc/dcn10/dcn10_mpc.c` | `7d61c9817f8460a12414352ea0fa424557a5ea06ef692f1d173129eac806d62e` | MIT | 手順（MPC の木の読み） |
| `drivers/gpu/drm/amd/display/dc/mpc/dcn20/dcn20_mpc.c` | `6bc22917a2a5987e317cc3ab40891212129feabf09ca36a9d600763217e29df0` | MIT | 読むだけ |
| `drivers/gpu/drm/amd/display/dc/dpp/dcn20/dcn20_dpp.c` | `50a4348765cb83a5ae6c113c21c97447bbe56eb9bd2375d3f993d54b9fc37429` | MIT | 手順（DPP の format、cursor） |
| `drivers/gpu/drm/amd/display/dc/dpp/dcn10/dcn10_dpp.c` | `85f9491c2e1e83c4324ad696b6e125eeb060741bf44299845ec9688abe594749` | MIT | 手順（cursor の位置） |
| `drivers/gpu/drm/amd/display/dc/dio/dcn10/dcn10_link_encoder.c` | `da115361a57013d981462759452eb9c49447454bb8463d7bb2708f57b35649db` | MIT | 値（DIG の mode・FE の select） |
| `drivers/gpu/drm/amd/display/dc/dio/dcn10/dcn10_stream_encoder.c` | `3322bc2491c2e0e60c04c0b2b11e6e419fb4fc71b07cac6099bdbdeb14306467` | MIT | 値（FE の source） |
| `drivers/gpu/drm/amd/display/dc/hwss/dcn20/dcn20_hwseq.c` | `03cf4aff1c4c53c8e8b417c9df49c090fda2b8c7febbb232102b1d73124a8765` | MIT | 読むだけ（init の挙動、power gating、blank） |
| `drivers/gpu/drm/amd/display/dc/hwss/dcn10/dcn10_hwseq.c` | `9b8c6c123bcb51ad9ce2d7aa9e4ad6db8cd603809297e3bc91799fa919e2f0e8` | MIT | 読むだけ（init pipes、cursor の順） |
| `drivers/gpu/drm/amd/display/dc/hwss/dce110/dce110_hwseq.c` | `05d914d6bbc1bef83c7090e194206a9cbe0c5a69ea25635db9647e8509aae311` | MIT | 読むだけ（accelerated mode） |
| `drivers/gpu/drm/amd/display/dc/opp/dcn20/dcn20_opp.c` | `4936196bc57d16b853611bf0272f7a10d74af73280903c513eedb5a47411e605` | MIT | 読むだけ（DPG） |
| `drivers/gpu/drm/amd/display/dc/hubbub/dcn20/dcn20_hubbub.c` | `402bf2e371a6503c091f79867e1464ef71ef980e92c6c348b42718ee624cfbde` | MIT | 読むだけ（FB・AGP の register、watermark） |
| `drivers/gpu/drm/amd/display/dc/hubbub/dcn10/dcn10_hubbub.c` | `87e2717da160a089c11c9b82adeaa6a2762354ea90c9362bb74a69c8e682a0d7` | MIT | 読むだけ（self refresh） |
| `drivers/gpu/drm/amd/display/dc/irq/dcn20/irq_service_dcn20.c` | `8f1b5df18f94b9d767b7849bdf7c9900569032a75b194b53c1122e4660140ce5` | MIT | 値（割り込みの enable・ack の register と bit） |
| `drivers/gpu/drm/amd/display/dc/irq/irq_service.c` | `e37299bd1079843ca6d484c19e71bc65e4400ea4e0f9e6cba78b9a973757fb25` | MIT | 読むだけ |
| `drivers/gpu/drm/amd/display/dc/core/dc.c` | `8cf89876b4829d3d205dd1712a2335cdd5cbb816a9f4219260dae49331667bc2` | MIT | 読むだけ（seamless boot の条件） |
| `drivers/gpu/drm/amd/display/dc/dce/dce_clock_source.c` | `6ba255ec514db542c971d557dd04b81228ec8bbcfe2916c61ccf05f06bb21e59` | MIT | 値（DP DTO の pixel clock） |
| `drivers/gpu/drm/amd/display/dc/clk_mgr/dcn20/dcn20_clk_mgr.c` | `0b967e2736ff597e7a21354bd325d82953d1ad0bdc03d1a4c07c67ef7cbf3900` | MIT | 読むだけ（SMU の hook） |
| `drivers/gpu/drm/amd/display/dc/bios/bios_parser_helper.c` | `15b3f14199c50224784e7d5c15b82aee07e68e880ee91037b1d10a99394ffafb` | MIT | 値（accelerated mode の bit） |
| `drivers/gpu/drm/amd/display/dc/dc_hw_types.h` | `8f0037727d62e0e5fb2bfc0f7acb99d6c01ae8d1955c9202b71dfbca583e62d8` | MIT | 値（cursor の mode、SW_MODE） |
| `drivers/gpu/drm/amd/display/dc/inc/hw/hubp.h` | `2a373fee62515a550662d2877103c4e5160f09a911fc74e96115a79fa5afb9f0` | MIT | 値（cursor の pitch・chunk） |
| `drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_0_0_offset.h` | `b32d5cbbf34bb39a93ff7513314db9a44352c9ba7b68d66a265b8b78490360cf` | MIT | 値（DCN の register） |
| `drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_0_0_sh_mask.h` | `ab89a83137abdb3ffa1ec22d4b813f2a653bfe3945586f8364a2fed3ef3194cd` | MIT | 値（DCN の field） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_display.c` | `f08bfe800223c5b43be9e90284994cc4adf489a1dc03877c751c46110e3d6190` | MIT | 値（linear の整列） |
| `drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.c` | `06941a7002b6682a8a782c07807a876a074afb6ec34755417f27230b5361099b` | MIT | 値（DCN の割り込みの client と src id）、読むだけ（gpu_vm_support） |
| `drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm_plane.c` | `171c8fd586827e079a1ebfb9b3f2e3b9e4555a5e4f1cc04b1637bdf5b8462c4f` | MIT | 値（cursor の属性） |
| `drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm_crtc.c` | `04790c9b651b4fbea74a1fed5c1f118df0f4cc3f376bb29114ff86d298216571` | MIT | 読むだけ（cursor の最大） |
| `include/uapi/drm/amdgpu_drm.h` | `31e52166e6124cfcc1abbad99d73f5199038f4cd4b48b9f133194a841b5498b3` | MIT | 値（family の番号） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.h` | `b4583234ff25eef4e8cd19d322e5cae3315af7348916f63d906930a51b5737f6` | MIT | 値（discovery の表の位置と大きさ） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_ih.h` | `61050183c8bed06be760d44200fc4f8b4bead290ccea239725ebf1cbfa10d9e2` | MIT | 値（IH の ring の大きさ） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_ttm.c` | `083cf56ece9db47310dbc0e21b93e617bca13529b1beeb5fa96ef7b73942d461` | MIT | 読むだけ（VRAM の予約、BAR0 と VRAM の offset、GART の bind） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu.h` | `dbb825405ee17feab730401ad218dbf1a06f28aa88436aee8e8293766b32d6d1` | MIT | 値（timeout、予約の大きさ） |
| `drivers/gpu/drm/amd/amdgpu/atom.c` | `56939066ae683215b79d2bf510194d72361ce530720a9ed0722be0d1db7418a9` | MIT | 手順・値（p014 の `bytecode.c`: operand の空間と幅、各命令の動き、register の小さな program、opcode の表。名前・構造は写さない）。host 試験の oracle として `build/tmp` に写して build（tree に入れない） |
| `drivers/gpu/drm/amd/amdgpu/atom.h` | `6157ba431d5fd2ce19fa40508becd293665ad4ada529b3f76179d509c2b9e3e6` | MIT | 値（image の印・header の位置・routine の header・operand の番号・state の dword・register の program の命令）、oracle の build |
| `drivers/gpu/drm/amd/include/atom-bits.h` | `d885526a0ebf5f47c4a0daadf3a0c9c6f3281b46d88cc857a98c01e893df6301` | MIT | oracle の build だけ |
| `drivers/gpu/drm/amd/amdgpu/atombios_i2c.c` | `5927a85cb1c9ef7235de74af1266c2c75e5c14a4e007aa31a294acad5bb63029` | MIT | 値・手順（VBIOS の I2C routine の速さ・上限・scratch の先頭の読み、p014） |
| `drivers/gpu/drm/amd/amdgpu/ObjectID.h` | `8f5a5dc001686ddae347332547f8767b83f33981a953877b95fabd8ca1313d4a` | MIT | 値（object の語の欄、connector・encoder の id、p014） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_amdkfd_gfx_v10.c` | `97722a4d5ed4faab2651e97bd30b4a5d025a33c0bb7d40c533855048191ee85b` | MIT | 手順（SQ_CMD の書き方、p013 の compute の reset の wave の kill） |
| `drivers/gpu/drm/amd/display/dc/dce/dce_i2c_hw.c` | `5e733d7ea46ad8798f729dbb3c067165f46d97a7497336e5e84126c9adf46fc1` | MIT | 手順（I2C engine の取得・設定・transaction・返却、p014） |
| `drivers/gpu/drm/amd/display/dc/dce/dce_i2c_hw.h` | `564cce137d26fa975dd83674984d9f8c10cf24d7431e081750cf1cd5561f5c5b` | MIT | 値（status の enum、time limit・reset 長・buffer） |
| `drivers/gpu/drm/amd/display/dc/dce/dce_aux.c` | `00f35c65631a0aefb4568c8294109bb0b7dde9c7e008cf22bb53e7fd0e26cf5d` | MIT | 手順（AUX の取得・request・reply、p014。実機は DP の sink が無く未確認） |
| `drivers/gpu/drm/amd/display/dc/gpio/hw_ddc.c` | `7c3416a412f4c006e876ebd3eb44a2fba01dcae975bbff799cf60bbe2c30f50e` | MIT | 手順（DDC の pad の I2C mode） |
| `drivers/gpu/drm/amd/display/dc/gpio/hw_gpio.c` | `47d230c8f6f46ec81a6815940b0365d02ccb626d4f0592896a6c3d6d2f404632` | MIT | 手順（pad の hardware mode、close の戻し） |
| `drivers/gpu/drm/amd/display/dc/bios/bios_parser2.c` | `28f672ed46b28bcabb2c268b6c47843e46dc63a7450c82fabf5c3f36f8728638` | MIT | 手順（path の record、I2C の id と pin の表、dce_info の crystal） |
| `drivers/gpu/drm/amd/display/dc/link/link_factory.c` | `55a597f1ccc70dd172b5d99f7bee0c2cebb9dbec1afed3da3373ebacff153dba` | MIT | 値（encoder の object と transmitter の対応） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_bios.c` | `163546c7f5a664c5e181d28127591559dcdf90dcd8ea3998056e61c8b8b5bf3e` | MIT | 読むだけ（VBIOS の取得、使わない） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_object.c` | `47dd9a3bee09d5d876ced5cd2b7076b1fc422ee45baf1bc7b5a91cb6a4724024` | MIT | 読むだけ（BO の GPU address） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_ids.h` | `72d2f2e9d5ca1a5ca6a28890d33eefb2a003025e58572e46eec48b63547f764b` | MIT | 値（VMID の数） |
| `drivers/gpu/drm/amd/amdgpu/gmc_v9_0.c` | `ce8588857b1d120b0e13653547dda1e363878461e0524c6a16db3e4251810a6c` | MIT | 読むだけ（translate_further が GFX9 だけである根拠） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_cs.c` | `a69131afab6c27707b8c104b3c734216f308838969a64e28b58134e5fc020220` | MIT | 読むだけ（root の PD の address） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_vcn.c` | `ca6e857d849244a40d1e9718d044392011ec07fcfc142c67e38c9b93d49085cc` | MIT | 読むだけ（VCN は範囲の外） |
| `drivers/gpu/drm/amd/amdgpu/vcn_v2_0.c` | `06ef3b9aac1f636ea5def38d79acf14b27d82e22780f451225f88503a81b17ab` | MIT | 読むだけ（VCN は MMHUB） |
| `drivers/gpu/drm/amd/amdgpu/soc21.c` | `362c549cec1efc14364cd89d234649a27e3747e8397e3071b84d103c98aea8fd` | MIT | 値（RDNA3 の external rev、認識だけ） |
| `drivers/gpu/drm/amd/amdgpu/amdgpu_cgs.c` | `fd4dfa206d66d3c46279cb2a430018661c4267fcea7729436a81f3976e3f3e1c` | MIT | 読むだけ（DC の register access） |
| `drivers/gpu/drm/amd/include/asic_reg/thm/thm_11_0_2_offset.h` | `233f62a308215ccf68c4ac8928ccc25bf4fcf8941661f64aff5a8f0a5a514b51` | MIT | 値（THM の温度の register） |
| `drivers/gpu/drm/amd/include/asic_reg/thm/thm_11_0_2_sh_mask.h` | `b534a519a8488ddd929ff4a98f327fc08e5a8d47d4b07eb4c684a9de1fc93ef5` | MIT | 値（温度の field） |
| `drivers/gpu/drm/amd/display/dc/dcn20/dcn20_vmid.c` | `baf71783929a93374ab4226e583d8e995546c9ea7e2308e7d9926390248f243f` | MIT | 読むだけ（DCN の VMID、使わない） |
| `drivers/gpu/drm/amd/display/dc/core/dc_vm_helper.c` | `c88bf46f06671539b95f007435824d5a1c75fda21522e55a00327a51f2a87414` | MIT | 読むだけ |
| `drivers/gpu/drm/amd/display/dc/dccg/dcn20/dcn20_dccg.c` | `ac064ac2c4bf420fd8b6e25042ef5eb0666876ef6609e651df780d0ad6ac7dd4` | MIT | 読むだけ（DPP の clock の DTO、触らない） |
| `drivers/gpu/drm/amd/include/asic_reg/smuio/smuio_11_0_0_offset.h` | `6e56a7eb344d3e68912889a0d8d0356136ded1f97146a2733eb9fd66028ff407` | MIT | 値（ROM の address・data の register。p002 で追加） |
| `drivers/gpu/drm/amd/include/atomfirmware.h` | `be8817aba46ed37ad412da72e5cf07d3777c850aa98f0463f85b1db7a8a53c10` | MIT | 値（rom header、data・command の表の並び、firmwareinfo v3.x、vram_usagebyfirmware、command の attribute。p002 で追加） |
| `drivers/gpu/drm/amd/pm/swsmu/inc/smu_v11_0_pptable.h` | `2a2975d08ce2b4f9b8867d77b32e3b3930e975929d7a7ede8f46c84a24da5fea` | MIT | 値（powerplay の表の header の欄。p002 で追加） |

取得範囲に無く note が名前だけ挙げた file（読んでいない、値を取らない）: `amdgpu/amdgpu_dpm.c`、`include/amd_asic_type.h`、`include/uapi/linux/kfd_ioctl.h`、`amdgpu/{cik,si,vi}.c`。

### 公開の規格（p014、値）

| 文書 | 取るデータ |
| --- | --- |
| VESA E-EDID Release A, Revision 2 | EDID の base block の layout、detailed timing・display descriptor・standard・established の timing |
| VESA DMT Version 1.0 Revision 13 | established・standard の timing の total と pixel clock |
| VESA E-DDC 1.2 | EDID の I2C の address |
| VESA DisplayPort 1.4 | AUX の command・16 byte、DPCD の 00000h〜00002h、8b/10b と link rate の単位 |
| CTA-861-G | extension の tag と detailed timing の offset |
| DVI 1.0、HDMI 1.4b | 165 MHz・340 MHz の上限 |

### host の表（値: device id・revision）

| file | SHA-256 | license | 取るデータ |
| --- | --- | --- | --- |
| `/usr/share/misc/pci.ids`（Version 2025.06.09） | `3b5b98dcbe3e42011c4c1a61897a8e82edde3865ce97f0da00b63e38324ce916` | GPL-2.0-or-later または BSD-3-Clause の選択（file の先頭の表記、2026-10-15 に確認） | 値（RDNA3 の device id、製品の名前） |
| libdrm `amdgpu.ids`（host の sysroot の写し、版 1.0.0 の表記） | `b752c65536b2de1fd01022ed1ae196788ce7c23f5f08071f87c39bb2821c0426` | 表記なし（libdrm の data file。libdrm は MIT。file 自体に license の行は無い、未確認） | 値（0x731F の revision ごとの製品） |

### Mesa 25.3.6（commit `06f9e28304d5d3f109c33535c1c25b9df5769af2`）

| path | SHA-256 | license | 取るデータ |
| --- | --- | --- | --- |
| `src/amd/common/amd_family.h` | `0a35bff826b4077c7d1c8b4d774670dc65af70da2fbba15ba850745de4118fd3` | MIT | 値（family・gfx level の enum、読むだけ） |
| `src/amd/common/ac_gpu_info.c` | `d2bbfc38876927cd30a864d239bf4f9b47c9f9f771e6888401b1e78af05ce817` | MIT | 値（bug flag、pc_lines、wave の上限）、読むだけ（識別） |
| `src/amd/addrlib/src/amdgpu_asic_addr.h` | `ed5e06b8e2ddf34aee762af54892e8d97341b4e9ed2891f230089fd5fe149300` | MIT | 値（external rev の範囲） |
| `src/amd/common/sid.h` | `ab108b90f8d4443eaf6742f1bf6f4454fc43bd5c9f3373cb1e02f1f0cf5308d3` | MIT | 値（PM4 の header・opcode・register の base・CONTEXT_CONTROL の bit） |
| `src/amd/common/ac_cmdbuf.c` | `eb6493c74dbb5149b35fa3664e46e27c1fdbcf44cb5784e3497b471c5d3e8371` | MIT | 値（preamble の register と値） |
| `src/amd/common/ac_descriptors.c` | `7d0b017e67126e303a3f7e5dd2d9b52da7aba52a082826d138386a5d7aef8483` | MIT | 値（T#・S#・V#・CB の register の組み立て） |
| `src/amd/common/ac_shader_util.c` | `ade4038a249a16eafd930990b875da6a478268a19b18bac4b0531f385f1f1699` | MIT | 値（export の format、late alloc） |
| `src/amd/common/ac_binary.c` | `d5e482ebd8bb0b2b8d3a03956c39700cccff6119e9872a1607118f10287469a8` | MIT | 値（float mode、prefetch の詰め物） |
| `src/amd/common/ac_binary.h` | `815afd9d817f9f98a9234ca2e739b8152bf1d399d8d2a25ef6bd9da1bd59891d` | MIT | 読むだけ（program の情報） |
| `src/amd/common/ac_formats.c` | `fd5f03381eeaa39556ef19a57274d307ec23bc4a0c8f49ade86c3c5324d8fd77` | MIT | 値（swap の規則） |
| `src/amd/common/ac_surface.c` | `6ab7c6a923c78f6e2410877bcb7db6bc6b9ae2056c78088b4cfb14a16d897a81` | MIT | 値（linear の pitch の規則） |
| `src/amd/common/gfx10_format_table.py` | `d55896f77692cd02f3924b530531d2e6bdaf0aeee3e73ef44109a016ab5c96ca` | MIT | 値（format の番号） |
| `src/amd/common/nir/ac_nir_prerast_utils.c` | `8cab36df507a0e2037823a037cf3b343c54c54b76bafca37564cc1e26aa45070` | MIT | 値（export の順、p007） |
| `src/amd/registers/gfx10.json` | `f3435695c47c3c3b1d8329a96b098452d5909944ca2c99995520c22614617d48` | 表記なし（Mesa の既定 MIT） | 値（register の field の bit） |
| `src/amd/registers/gfx10-rsrc.json` | `ab6deb6cea2daf6ef546b8ae5eff105d5939c9d8cd76a710f9f8064e6a2cf820` | 表記なし（Mesa の既定 MIT） | 値（記述子の field） |
| `src/amd/registers/gfx8.json` | `225dcbb9c4d16cc4c79d48e1b91d19ab71afa24f6a14e3613ddc49379c08d1bb` | 表記なし（Mesa の既定 MIT） | 値（swizzle・sampler の enum） |
| `src/amd/registers/pkt3.json` | `36f5c2eba891df4ba1f85807241d536e8e4cc6787e34328971f071f4cddf0242` | 表記なし（Mesa の既定 MIT） | 値（packet の field） |
| `src/amd/vulkan/radv_queue.c` | `5dd76f766aa695e7a2020534b5a5611a732e1692b386a514676c1a331781d454` | MIT | 値（preamble の順と値） |
| `src/amd/vulkan/radv_cs.c` | `ffb9b50e6466e915d42e3f9de5b491c1f65b547045d4a82b24018ad630b4affe` | MIT | 値（cache の flush の packet） |
| `src/amd/vulkan/radv_cs.h` | `28909e0689d8989d67837bc2efd1902a854c5442933cf3f09045027c0e543270` | MIT | 値（SET_*_REG の index の形） |
| `src/amd/vulkan/radv_cmd_buffer.c` | `55eada950f7050e4ad46d7ed57f30a10312c9ec98d8a0edd5621fb7cb54fcd88` | MIT | 値（draw の state の register と値） |
| `src/amd/vulkan/radv_shader.c` | `50cd555783d1a33407e21b595dfb5c4a48f9f8394cd707bdb5e78aebbf240556` | MIT | 値（RSRC の field、shader の整列） |
| `src/amd/vulkan/radv_shader_args.c` | `8787dfd790ace99e1d908a3293872d516a90a65a0501d07d5f43cd8c72052381` | MIT | 値（入力の register の並び、user SGPR の順） |
| `src/amd/vulkan/radv_shader_args.h` | `d4ab969972930191dfbf6b8f52e0b28c545ee0d027c12aa33686f2d195caf7e5` | MIT | 読むだけ |
| `src/amd/vulkan/radv_shader_info.c` | `3af53f42ea08f7631865ffd4fb3011bc3ff2df499b46713d138893e1645691ad` | MIT | 値（user data の base） |
| `src/amd/vulkan/radv_physical_device.c` | `c98f9882d5ddda4054745d5d93c9edad9f40ab70e7286960b23eda9f2d482c7a` | MIT | 読むだけ（NGG・wave の既定） |
| `src/amd/vulkan/radv_pipeline_graphics.c` | `90af912295c28a2aaae019b886dcd468d20e28318df2ee81b3ca6fbebead57d0` | MIT | 値（blend の値） |
| `src/amd/vulkan/radv_pipeline_compute.c` | `7815d29ffbbc7fd4c21ce8ca139cd7ce8b0bf29d2a31899bdddd34ce5a1f734a` | MIT | 値（resource limits） |
| `src/amd/vulkan/radv_image_view.c` | `7c02ad1cf260c64e6189b8322baa1e035e6e4fa3d568e1231b8f1ea300543719` | MIT | 値（swizzle） |
| `src/amd/vulkan/radv_sampler.c` | `35e278cbd5fddd12e77ae51f18b6eeac336534a88c1ab6e71ec004b958b209af` | MIT | 値（sampler の field） |
| `src/amd/vulkan/radv_descriptors.h` | `7dade205625c789c1ffd4fbc13becd8f4308ae027800d94383952ee3b4f797b5` | MIT | 読むだけ |
| `src/amd/vulkan/radv_constants.h` | `9014dad0c27cd37ffa0887d4904327db299cee078e05a5ce324ed954ec520ac1` | MIT | 値（整列） |
| `src/amd/vulkan/radv_cp_dma.c` | `bfb57c8e4948ae354bdcf0fe0cdbaad31d3f1b4a680fe084d6b78422554ebe09` | MIT | 値（DMA_DATA の field） |
| `src/amd/vulkan/nir/radv_nir_lower_vs_inputs.c` | `6e3164550dc8ba050e728b30798b9dc6c408ffd1a3677c1152d89b214bf83092` | MIT | 読むだけ（頂点の fetch の形） |
| `src/amd/vulkan/meta/radv_meta_clear.c` | `d7b11cf4c0f51ec51320b4dec632a8e890bd05e095167964a43bdd3b2fe8c7d3` | MIT | 読むだけ（clear の道） |
| `src/amd/vulkan/meta/radv_meta_copy.c` | `84e25e331708f85f30a9ad5280fca653abf4d0092b36b914280e49c9072fb596` | MIT | 読むだけ（copy の道） |
| `src/amd/vulkan/meta/radv_meta_buffer.c` | `e9e8c38b3748d81cf3a7cdbeecd34e6dad60052f035273cb1324dfcbe4822ea3` | MIT | 読むだけ |
| `src/amd/compiler/aco_assembler.cpp` | `944b22c4282823e9aa57571ff22e1ae30d12c1b876f8b8747b41ddd82f8b5cd8` | MIT | 値（export の done・vm、s_code_end、p007） |
| `src/amd/compiler/aco_opcodes.py` | `6ecb66d935ea1b3137bc55c88713c876bc088f04ef2b21d36af80c4dd38823ca` | MIT | 値（opcode、p007） |
| `src/amd/compiler/aco_interface.cpp` | `802b2d488a6a6b9eec190cddbd437e12fadbfe0326326ded26ebf6f9db7ef433` | MIT | 読むだけ |
| `src/amd/compiler/aco_register_allocation.cpp` | `c67ae6fe9f53a627be9f3663935a3afdb1702e0db42fe4f6d26888c56665412d` | MIT | 値（granule） |
| `src/amd/compiler/aco_ir.cpp` | `b86d6f9d61a0c1dae6587b721171287e32128e967231b11a24ed1f2b7f1b013c` | MIT | 値（SGPR の上限） |
| `src/amd/addrlib/src/gfx10/gfx10addrlib.cpp` | `004c23082eb7259909e2a55e73f2300bd3da206f8349c4293140fd7749ff8081` | MIT | 値（linear の pitch の整列） |
| `src/amd/addrlib/inc/addrtypes.h` | `cfd2d05ebd45905e6326f021cda3f893c748e3ac0db0ca77feceed4c5ccb7423` | MIT | 値（resource type） |
| `docs/license.rst` | `0d1a0472ecc81830e75c20d59b0ea02841e3db21255e0ebad97ab682c54d6615` | 表記なし（Mesa の既定 MIT） | 読むだけ（license の既定） |

### linux-firmware 20260410（commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120`）

全部 `LICENSE.amdgpu`（再配布の条件つき）。package に入れる 11 file（`navi10_mec2.bin` は `navi10_mec.bin` と同じ byte）。`navi10_ta.bin`・`navi10_vcn.bin`・`navi10_gpu_info.bin` は入れない（design の 4.1）。大きさと SHA-256 は [phase001/phase.md](phase001/phase.md) の表（2026-10-15 に再計算して一致を確かめた）。

| file | SHA-256 | 扱い |
| --- | --- | --- |
| `amdgpu/navi10_sos.bin` | `3112ea8519545ac89c1623e2e08e841a6a9bc31c2aab2f9b787979e51277e395` | package、A2 の bootloader と TOC |
| `amdgpu/navi10_asd.bin` | `2d084493e763e60cd90ac25b054f73e4857a24864334cd3c5b092f3273cd12de` | package、LOAD_ASD |
| `amdgpu/navi10_smc.bin` | `49ed339d0ff71fe64347380ee12b00c3c69a546f454bcd4323c411c72fe6fb2e` | package、LOAD_IP_FW |
| `amdgpu/navi10_pfp.bin` | `310032fe0698871b80be6eb5877ae7ed3395cb712629698c5c631e0af7c4e04a` | package、LOAD_IP_FW |
| `amdgpu/navi10_me.bin` | `faf2f9ff33580b53f7b90f410447efa1d2b21366f344ff5a383d074bf1ea8c35` | package、LOAD_IP_FW |
| `amdgpu/navi10_ce.bin` | `074ec5270eb7cad73906a1a591c274a40895b170091bc8068180a4c4d7771165` | package、LOAD_IP_FW |
| `amdgpu/navi10_mec.bin` | `936de4938bcfd5e1691b1e544247000f7b4b7430eef317e28f8560bb9e88e758` | package、LOAD_IP_FW（JT を除く） |
| `amdgpu/navi10_mec2.bin` | `936de4938bcfd5e1691b1e544247000f7b4b7430eef317e28f8560bb9e88e758` | package、LOAD_IP_FW（JT を除く） |
| `amdgpu/navi10_rlc.bin` | `1f52478fb78dee904361b93ad67955fcff151bb7a7772bbfbd2c448c814be1f5` | package、LOAD_IP_FW ×4 |
| `amdgpu/navi10_sdma.bin` | `3fc635b9a17819eaf13913882676f0cd07a6c1a31c87b3ef0dcedebf594c8dd0` | package、LOAD_IP_FW |
| `amdgpu/navi10_sdma1.bin` | `317b398d0563c8c89b10c31712296843143385f5a1ddcee2031536fe67576c3b` | package、LOAD_IP_FW |
| `LICENSE.amdgpu` | `572872598565dc3513470de971a32bf9db301f47afeef3636345eadae33b2eee` | package に写す |
| `WHENCE` | `c282239a5a2d849677e9304e6f361e475e1b6e71e7c771c03f8986f71b309527` | 該当の行を package に写す |

## 残課題（p010 で）

- 参照した file の全文の license の確認（先頭だけの機械判定の補い）。
- 実装が新たに参照した file の追加（各 Phase の終わりに表へ足す）。
- 13.3 の監査の script の結果（symbol の交差が空、出典の comment の有無）を記録する。
