# WS014 p006 / q309 の受入結果

## q309完了: GPU handle共有・Wayland WSI・virtio scanout（2026-09-13）

WS014 p006 / q309-i01をcleared、q309をfinishedとする。active Queueなし。WS014はincomplete、p001/p004 planning、p004未queue。WS030 completedと既存Phaseのclearanceを維持し、native i915は別WS029のまま。

kernel_handle/handle_fd_*と共通fd参照・SCM_RIGHTS、GPU/Venusの独立process/context共有、VK_KHR_wayland_surface、最小libwayland-client.so、全画面zwl、標準Wayland/Vulkanアプリwltestを実装した。共有GPU imageはGPU copyと所有権同期を経て別processへ渡り、SET_SCANOUT_BLOB/RESOURCE_FLUSHで表示する。通常の新WSI経路にCPU readbackや再uploadを必須としない。旧copy経路もGOPではなくvirtio 2D scanoutであり、直接表示の互換経路として保持する。

GET_DISPLAY_INFOとGET_EDIDのbase/CTA progressive DTDで表示・モードを列挙。実QEMUでは1280×800、74,994mHz、320×200mmを取得した。custom framebuffer寸法はEDID寸法と独立に扱い、1〜100Hzのguest nominal pacingを検証する。virtioは物理pixel clockを設定しないため、物理vblank同期の保証とは区別する。

最終q309-wayland-004は43.869秒、QEMU exit0でPASS。FIFO/MAILBOX各6枚の320×240実VNC画像、計921,600画素が独立期待値と全画素一致し、12回の表示が実import資源とBLOB scanoutに対応した。生成元終了後の独立renderer import/GPU copyも検証専用readbackの1024画素が一致。swapchain再作成、client中断・再open、compositor通常終了・SIGKILL・再起動、実SURFACE_LOST/cleanup=0、強制終了直後の640×480 console復帰とechoによる画面更新を確認した。

同じ最終kernelのq309-direct-002も42.705秒、QEMU exit0でPASS。標準vkdemoの回転直方体6枚をGPU readback/VNC/独立oracleで照合し、通常終了とSIGINT後の再open、console復帰、表示競合拒否とowner完走を確認した。K/fd/SCM・GPU/EDID・Wayland/WSIの実コード限定fixtureとsanitizer、157 Vulkan dispatch/export、両ABIの公式header照合、Noct再生成、rootfs配置、対象buildと全適用規約を確認した。正式CTSや全Wayland SDK互換は主張しない。

途中のharness起動待ち不足とEDID/custom mode回帰を修正し、失敗証拠と再実行理由を保存した。最終reviewのMSG_PEEK二重put疑義は、rights付きpeekを既存guardが拒否するため到達不能と確認し、本体変更を戻して拒否後の参照寿命を追加検証した。formatterは規約と設定の不一致によりexit1でありPASSとは扱わず、全文確認とdiffcheckを記録した。HAL追加変更なし。ローカル結果はplan/ws014/phase006/results.md、技術資料3件、conformance.mdとfinal-evidence/verification.json、Queue履歴はplan/history/queue-q309.md。これらsource/doc/imageのgit add/commit/pushはユーザーが行う。本同期はGitHub Issues/Projectの計画・受入結果である。

## 実装と受入条件の対応

| 完了条件 | 実装と証拠 |
| --- | --- |
| VFSを必要としない型付きobject fd | include/kern/handle.h、src/kern/handle.c、共通fd-object。table内get、lock外release、close/dup/fork/exec/CLOEXEC/CLOFORK、SCM予約/copyout/commit/rollbackの実fixture |
| 別process/contextの同一allocation | GPU export/import、独立した保管オブジェクトとalias。producer終了後のreceiverが自分のVkImageへmemoryをbindし、GPU copyの1024画素を照合 |
| GPU内の共有表示 | optimal→linear GPU copy、EXTERNAL ownership、実fence待ち、SCM_RIGHTS→zwl import→SET_SCANOUT_BLOB。12表示のimport resource IDとBLOB flagsを対応付け |
| Wayland標準アプリ | wltestは標準Wayland/xdg-shellとVulkanのみ。private buffer factory、GPU ioctl、Venus wireはlibrary/K境界に封じる |
| WSIの同期と寿命 | private queue/wrapper、frameとbuffer.releaseの分離、FIFO/MAILBOX、acquire/timeout、再作成、surface loss、OOMと部分破棄の限定意味fixture |
| 切断とconsole復帰 | 実client SIGINT、compositor TERM/SIGKILL、失敗処理cleanup=0と再open。SIGKILL直後/最終停止後とも640×480、echoで画素変化 |
| 既存直接表示を保持 | 最終kernelで標準vkdemo6枚、独立ray/texture oracle、正常/中断再起動、表示競合、consoleを再確認 |
| 配置と公開境界 | /lib/libvulkan.so、/lib/libwayland-client.so、/bin/wltest、/bin/zwl。ELF SONAME/依存と実rootfsのhash、Vulkan137 core+20 WSI=157 exports |

## 最終実行環境と再現

private awe@10.0.10.25上のQEMU10.0.11、virglrenderer1.1.0、Intel ANV/i915を使用した。KVM pc、amd64、1GiB memfd、2 vCPU、OVMF4M、virtio-vga-gl venus/blob、hostmem256MiB、max_outputs=1。GL画面の照合はegl-headlessのVNC Unix RAW、console制御と情報取得はQMP。ホスト取得のreadbackをguest表示のCPUコピーと混同しない。ホストsystem packageの追加変更は行わない。

```sh
timeout 1400 python3 plan/ws014/tests/run-vkdemo-remote.py --attempt q309-direct-002 --build-directory /home/awe/zedBSD/build/vkdemo-amd64 --config /home/awe/zedBSD/plan/ws014/tests/config-wayland-amd64.mk --build-timeout 1200 --timeout 180 --lifecycle
timeout 1400 python3 plan/ws014/tests/run-wayland-remote.py --attempt q309-wayland-004 --build-directory /home/awe/zedBSD/build/vkdemo-amd64 --config /home/awe/zedBSD/plan/ws014/tests/config-wayland-amd64.mk --build-timeout 1200 --timeout 180 --lifecycle
```

各wrapperはmake -j16 disk-image、source前後hash、kernel差し替えと専用shell起動image、転送、有限VM、取得画像の再照合を行う。attempt directoryは再利用せず、実行時は次の未使用IDを指定する。最終実行sourceに以後のproduction変更がないこと、二つのVMのkernelが一致することをfinal-evidenceへ記録した。詳細argv、firmware/renderer/image/source hashes、全画像は各attemptのresult.jsonとevidenceに保存している。

| Wayland最終成果物 | SHA256 |
| --- | --- |
| application | `30846d119278fba7bf83f06fd3d7f5eff7426a1d9ef98d114259f244a9f567bb` |
| base_image | `6d51914b8ae6c2a05fe9a0f9ee11d1611209ed4d5d1be7bcf9c6c3efc4f15f32` |
| compositor | `52eb058231977c42ffdb8956fc85e929d25670e2f5cedc6adff3ddc42fc930a9` |
| kernel | `2714fa1f35058ce379f2bcaf02f69213e9ceb6b697f77cd45995ca45eb25c26c` |
| sharing_test | `0fc2a3a854b00820d1c4ed89bbfb3dce420b220d8f1ce90019f61d8da7f3dc2f` |
| vulkan_library | `2b7f69fad230e1cdb4cc02b69936a50988ab3bbb12c69e65fa677136f1b12ca7` |
| wayland_library | `1b087671feefa3a15f7eb83de58f8180295ff25b3914cba94d0c71466f6520e0` |

source snapshot SHA256: `1aee2cb1e16e71268446f172d4515512468b562bd19898e2cab2605da14ff952`。baseline commit: `a0032e30d7f71937fc2c6faa911331ba5847dc0c`、差分は未commit。

## 実行履歴と修正理由

| attempt | 実VM結果 | 意味 |
| --- | --- | --- |
| q309-wayland-001 | PASS / 21.257秒 | 最初の独立import・12枚全画素一致。最終lifecycleの前 |
| q309-wayland-002 | FAIL / 182.257秒 | 描画・SIGKILL/surface lossは成立したが、harnessがrm後のshell prompt前に次のcommandを送った。prompt待ちを追加 |
| q309-wayland-003 | PASS / 43.887秒 | lifecycle全体と実EDID。custom mode回帰修正前 |
| q309-direct-001 | FAIL / 4.812秒 | vkdemoが実列挙74,994mHzでcustom320×240を要求すると、誤ったEDID寸法一致条件で拒否。virtio rectangle契約とguest nominal pacingへ修正 |
| q309-direct-002 | PASS / 42.705秒 | custom mode修正後、最終kernelの6枚とlifecycle/競合回帰 |
| q309-wayland-004 | PASS / 43.869秒 | 同じ最終kernelで全Wayland受入 |

最後の独立reviewでfile専用互換receive wrapperのMSG_PEEKに二重putの疑義を挙げたが、呼出先の既存guardがrights付きpeekをEOPNOTSUPPで拒否し、疑った経路へ到達しないことを確認した。追加の本体変更は元へ戻した。「peek成功」の新fixtureも現契約に反する期待だったため訂正し、rights付きpeek拒否後もqueued fileが生存し、通常受信/掃除時の最終releaseが一度だけであることを通常・sanitizerで追加確認した。これは実害の修正ではない。最終production sourceは上記二つの受入VMと同一であり、不要なVM再試行はしていない。

旧transport負例の256MiBはq308上限の範囲内だったため512MiBへ訂正し、拒否-before-mapを保持した。旧memory fixtureはblob flagsを渡すhelperへの更新が必要だった。これらfixtureの訂正理由と以前の失敗ログを保存し、production能力や画素oracleの期待を緩めていない。

## 限定検証・設計資料

- [K handle/fd/SCMとzwl](kernel-compositor.md): 実コードと実socketの参照寿命、copyout失敗、切断、wire、configure/unmap、listenerの回収。
- [GPU/Venusとnative scanout](gpu-sharing.md): 型/範囲/別GPU拒否、alias/scanout参照、renderer import、EDID、nominal pacing、consoleとtransport。
- [Wayland clientとVulkan WSI](wayland-implementation.md): ABI、partial I/OとSCM、queue/wrapper、WSI失敗境界、157 dispatch、採用revisionと制限。
- [規約確認](conformance.md): 全§1–14、独立review、Noct6出力再生成、target build、analyzer、formatterと手動基準の差。
- [最終機械記録](final-evidence/verification.json): final runs全record、source/artifact/installed hash、Noct照合。
- [直接表示の実証拠](../temp/remote/q309-direct-002/result.json) / [Waylandの実証拠](../temp/remote/q309-wayland-004/result.json)。各evidence/にVNC画像、独立oracle JSON、console/observed log、QMP履歴を保持。

## p004へ渡す境界

初版はamd64・同一GPU・linear RGBA8/BGRA8・単一全画面。factoryにGPU identity/capability広告がないためmulti-GPU選択は未実装。client libraryは選択したcore/xdg-shell/private factoryの最小実装であり、入力、wl_shm、subcompositor、一般DE、全SDK対応を広告しない。公開external-memory/fence拡張やnative i915は含めない。

nominal refreshは1〜100Hz、EDIDはbase/CTA progressive DTD、guest時計は100Hzの分数tick pacing。物理vblankや全層zero-copyを保証しない。FIFO watchdog10秒、共有1plane/各辺16〜4096/256MiB上限、初期buffer factoryと8-bit linear formatの制約をruntime validationで守る。WS014全体の最終API整理はp004で別に行う。p006の受入をp004のclearanceには流用しない。
