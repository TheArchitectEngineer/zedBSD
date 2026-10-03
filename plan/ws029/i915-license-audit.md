# Linux i915 参照ファイルのライセンス監査（tag v6.19）

取得元 `https://raw.githubusercontent.com/torvalds/linux/v6.19/`。判定は先頭 40 行の SPDX 行または MIT permission notice の機械判定。MIT 以外が 1 つでもあれば本 script は失敗する。

| path | SHA256 | 判定 | 根拠 |
| --- | --- | --- | --- |
| i915_reg.h | `756e332354a4be1eb62edea220127c632eadb8ebdbe83c558d158476395ce0f0` | MIT | MIT/X11 permission notice |
| i915_reg_defs.h | `375a8d3b52a9ad462ac7f6dae60c6c2775ccf4e97d95e1584d4059f2958448a7` | MIT | SPDX MIT |
| intel_uncore.c | `a06135c78f63eb8d35f7f9ed4e7952dca99f0e155819775be1eb35ea9519cf8f` | MIT | MIT/X11 permission notice |
| intel_pci_config.h | `f792fd35a880fc179a4d556b78125466e8ff0668f7aaaa7957c286bc8fbdd462` | MIT | SPDX MIT |
| i915_pci.c | `3c90eb2b333e0eb94fbc5770dcceb69453e1816448f1efdd90508bea87e32813` | MIT | MIT/X11 permission notice |
| gt/intel_gt_regs.h | `2d131eb5839396c099e8c63b52e8f998407936d8a34df4a547080fc13f5a91b9` | MIT | SPDX MIT |
| gt/intel_engine_regs.h | `e6ae20bc2946a252a948628df6a84522c19dd45d537f5efa0e5c55d08e82961a` | MIT | SPDX MIT |
| gt/intel_lrc_reg.h | `671b2adfb2825597425eea515b892cba1365f658d552f9a90439182f0f7f21c0` | MIT | SPDX MIT |
| gt/intel_gpu_commands.h | `d8d14b3edefb858418eb52506f5f7598ee2cd6bd8affa659e43faaf11c85e77c` | MIT | SPDX MIT |
| gt/intel_gtt.h | `f3b92ef5fc03006fea2f84f50e6c7f0a05096b82846e18329cb2a81fa4a411b0` | MIT | SPDX MIT |
| gt/intel_lrc.c | `76817c337bbf7292de60b6050b963bc938aaac33e12a390058af0874c88eef5b` | MIT | SPDX MIT |
| gt/intel_execlists_submission.c | `f017c05d94d69d03506204610d108190a4e78b0a2ee8ee9267e4438a95adc3fd` | MIT | SPDX MIT |
| gt/intel_reset.c | `38488b71985a2a1aa86d5d81862a82486cf86e0c92e9dc2b8df1538418919067` | MIT | SPDX MIT |
| gt/gen8_ppgtt.c | `b7afa1fdccc6ea791a333c612a27a2778b69f73b2964d963b4bfcb87ffb92531` | MIT | SPDX MIT |
| gt/intel_ggtt.c | `3824561af0e1659c8f022933c734542d3fd1d7e8e696c4e09490e246369fdd69` | MIT | SPDX MIT |
| gt/intel_gt_irq.c | `2461ff2b780ed88da5ae4e58ff6adfb5f3469fa44626cf802484175a42e26668` | MIT | SPDX MIT |
| gt/intel_engine_cs.c | `aacc2f24da117cdb95b61d12933540701558504317269ad62028aafd1205db48` | MIT | SPDX MIT |
| gt/gen8_engine_cs.c | `d605b5e4b74d76e29be1db9fb22b2f16d52f8ed96ad2c5746f850665f10f2c72` | MIT | SPDX MIT |
| gt/gen8_engine_cs.h | `5d3def4da1e5469c10f4be1471574e01510d3353de3aae891fdfb4d2343c8d26` | MIT | SPDX MIT |
| gt/intel_mocs.c | `dc367b364d8a9242967ec00f6456b0ed4960383761936207b4b6b0acd3e34f21` | MIT | SPDX MIT |
| gt/intel_engine_types.h | `47b77f01148818361cf0f0c02bb40fd6420b06941d6e06516e9447f3146b6a62` | MIT | SPDX MIT |
| gt/intel_context_types.h | `999dc3bf65b9f73ed08dd8210b41787c135922f3bbddbc6461e774cb755320f4` | MIT | SPDX MIT |
| gt/intel_lrc.h | `e71534746b6f6720c978c0096316c51873f5afc2974d94cd4979303384a6f5b3` | MIT | SPDX MIT |
| gt/intel_engine.h | `d07100bbdd00a2874b19851134c56b56c448df0219be67acb01e89ab3ed996cc` | MIT | SPDX MIT |
| i915_irq.c | `dd50909e1b8a87112a7881e810ae8e045d457141299da13ee28b61d178bec167` | MIT | MIT/X11 permission notice |
| gt/intel_gtt.c | `87269ff9bd39cb11c44f73bc76c1bdaf494781d277f4f0822d196409b626ecad` | MIT | SPDX MIT |
| include/drm/intel/pciids.h | `58d728ce4ac241f8485907cbd466d7dcd238fddf0008d09e3b7dd47ccf52bb85` | MIT | MIT/X11 permission notice |
| include/drm/intel/i915_drm.h | `46a108e9401fd36f2405db8f70f84dddeca8ddc76fc2a33b53a0d53608c0e82e` | MIT | MIT/X11 permission notice |
| include/uapi/drm/i915_drm.h | `6a6c165125a03132fc6f616cd0dc7938b7d8a81b01073e19f8b9fd4bcbc19e0e` | MIT | MIT/X11 permission notice |

生成: 2026-09-14T01:58:20Z、script plan/ws029/tests/fetch-linux-refs.sh、結果 全ファイル MIT。
