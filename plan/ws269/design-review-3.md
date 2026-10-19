<!-- design-reviewer の差分の確認（2026-10-19、ws269 design.md 改訂 3）。Q1 が報告の本文をそのまま置いた -->

## WS269 p001 改訂 3 の差分の確認

対象は `/home/awe/zedBSD-claude1/plan/ws269/design.md` の版 3（commit 7476bb5d9）です。`git diff 044895607 7476bb5d9 -- plan/ws269`（design.md、ws.md、phase001/phase.md）を読み、次と照らしました。

- zedBSD: `include/drivers/gpu/gpu.h`・`gpu-scanout.h`・`gpu-share.h`・`gpu-display.h`、`src/drivers/gpu/gpu.c`（recovery の監視 7085-7265）、`bcm2711/display-device.c`（import_image）、`include/hal/hal.h:653-680`、`src/hal/riscv64/page.c:403-433`、`src/kern/uncached.c`、`libvulkan/instance.c`・`memory.c`・`wsi-image.c`、`plan/ws256/design-peripherals.md` §2.4、`plan/ws256/phase023/phase.md`、`plan/future-work.md`
- `~/visionfive-linux`: mainline の imagination（`pvr_mmu.c`・`pvr_fw_startstop.c`・`pvr_vm_mips.c`・`pvr_fw.c`・`pvr_rogue_fwif.h`）、vendor の `sf_7110/sysconfig.c`、`rgxinit.c`・`rgxcompute.c`・`rgxmipsmmuinit.c`

file は変更していません。build・試験・QEMU は実行していません。firmware は読んでいません。

### レビュー 2 の各項目の確認

| 指摘 | 判定 | 補足 |
| --- | --- | --- |
| Major 1（coherency の判定・対照・SLC） | 文面は直った。試験の中身に新しい穴がある | 判定の規則、0b、64 KiB × 8、INCONCLUSIVE の時は切り替えない、は入りました。ただし p006 で使える道具と GPU 側の属性が決まっておらず、SLC の問いにも答えられません（下の Major 2） |
| Major 2（present と invalidate） | 半分直った | render の target を uncached の写像にし、damage の矩形だけを写し、L2 の追い出しを記録した点は正しいです。ただし display の node は「その写像」を持てません（下の Major 1）。HOST_CACHED の扱いは既存の libvulkan と合いません（Minor 2） |
| Major 3（recovery の契約） | おおむね直った | 細部は Minor 3 |
| Minor 1・3・4・5・7・8・9・10・11・12 | 直った | 古い文が残っています（Minor 6） |
| Minor 2（表と 6 の矛盾） | 中身は直った | 表が壊れています（Minor 1） |
| Minor 6（依存の写し） | ws.md と WS256 p023 の phase.md は直った | WS256 の ws.md・design-peripherals.md が古いままです（Minor 7） |

---

### Blocker

なし。

### Major

**M1（新規）. present の CPU の copy で、display の node が render の buffer の uncached の写像を手にする道が無い**

- 根拠:
  - design.md:160（§3.5）は「copy はその写像（render の `drv_dma_alloc_coherent` の uncached の写像）から読む」と書き、display の node と render の node を別の device にしています。
  - しかし display の node が foreign の buffer を受け取る口は `scanout.import_image` で、渡るのは `struct drv_gpu_scanout_backing`（物理の page の配列と flags）だけです（gpu-scanout.h の `drv_gpu_scanout_backing`・`import_image` の注）。kernel の VA は渡りません。
  - 自分で写すなら `hal_pmem_map_uncached` を使うことになります。hal.h:653-680 は「caller owns」の managed RAM を求め、unmap の後は「direct map だけで届く」と定めています。借りた page を 2 重に写すのは、契約の文面の外です。
  - Pi 4 は、同じ driver の中の private な lookup（`bcm2711_shared_lookup`、display-device.c:1789-1815）で、render の buffer の object そのものを retain しています。VF2 の display の node は §3 で「GPU の core と独立」とされているので、この道もまだ無いです。
- 起こる状況:
  - 実装者が direct map で読むと、hal.h:653-662 に違反し、古い画を出します。
  - 写す道を p007 の中で作ると、§6 の「p007 は p004 の後なら p005・p006 と並行できる」（design.md:196）が崩れます。import の相手（render の `share`）は p006 で出来るからです。
- 重大度: Major（API の不一致と Phase の依存の誤り）。
- 修正案: 次のどちらかを §3.5 と §6 に決めて書く。
  - (a) display の node を powervr と同じ driver の中に置き、Pi 4 と同じ private な lookup で render の buffer の object を retain し、その kernel の uncached の VA から読む。p007 の import の部分は p006 に依存させる。
  - (b) 借りた page を display の node が `hal_pmem_map_uncached` で写す。この場合は、hal.h の「caller owns」と unmap の文面との関係を §3.6 で分析し、文面の変更が要るなら proposed/ に置いて止まる。
  - どちらの場合も、p007 の試験は自分の resource での claim・present に限り、import の試験は p010 に置く、と §6 に書く。

**M2（新規）. §3.2 の 4 の主の試験は、p006 で使える道具と GPU 側の属性が決まっていない。SLC の問いにも答えられない**

- 根拠と起こる状況:
  1. **道具**: design.md:122 は「最初は FW の transfer か、無ければ FW の memory の書き込みの命令の繰り返し」と書いています。
     - §9（design.md:227）は「TQ が USC を要するか」を未確認とし、transfer を p010 に置いています。p006 では transfer はまず使えません。
     - 残る道は、UFO（4 byte）の更新を 16,384 個 × 8 回並べる事です。UFO を書くのは MIPS の FW processor です。この試験で確かめられるのは MIPS の道の coherency だけで、GPU の data master（TA・3D・compute・TQ、MMU の table walk）の道ではありません。
     - 「4 byte の UFO だけでは足りない」（同じ行）と、事実上の道具が UFO である事が食い違っています。
  2. **GPU 側の属性**: GPU が snoop するかは bus の固定の性質ではなく、transaction の属性で決まります。
     - Rogue の PTE には CC（cache coherent）の bit と SLC_BYPASS の bit があります（mainline の `pvr_rogue_mmu_defs.h:100`、`pvr_mmu.c:763-772`）。mainline は CC を常に 0 にしています（`pvr_mmu.c:2594`）。
     - AXI の ACE-Lite の domain の設定（`pvr_fw_startstop.c:22-33`）も関わります。
     - MIPS の写像の cache の policy（`pvr_vm_mips.c:175-176`。mainline は host と共有する FW の object を `PVR_BO_FW_FLAGS_DEVICE_UNCACHED` にしています: `pvr_fw.c:295`・`463`〜`659`）も関わります。
     - 設計はこれらを試験にも製品にも決めていません。試験を CC=1 で流して「coherent」と出し、製品は CC=0 のまま既定を切り替えると、黙って壊れます。
  3. **SLC**: design.md:116 は「完了の通知が SLC の書き出しを含むかを p006 の 4 の確かめで調べる」と書いています。一方、design.md:122 の主の試験は、読む前に必ず SLC の flush を入れます。これでは問いに答えが出ません。
     - flush を省いた変形で古い値が出なくても、「含む」の証明にはなりません（64 KiB は 16 KiB の SLC を何度も追い出します）。
     - mainline は `ROGUE_FWIF_KCCB_CMD_SLCFLUSHINVAL` を enum に持つだけで、送る所はありません（`pvr_rogue_fwif.h:1160`、imagination/*.c に送り手は 0 件）。vendor も、送るのは UFO の解放の時と compute の flush の要求の時だけです（rgxinit.c:3630-3651、rgxcompute.c:1095-1102）。
- 重大度: Major（試験計画で検出できない誤り。誤った一般化は、レビュー 2 の Major 1 と同じ「既定を coherent に倒す」害につながる）。
- 修正案:
  - 主の試験の対象を「どの requester の道か」で名前を付ける。p006 では MIPS（UFO）の道だけを確かめ、その結果で切り替えてよいのは MIPS の道（FW の構造）だけ、と書く。GPU の data master の道の確かめは、compute の kick ができる p008 で同じ手順を流す。
  - 試験と製品で、PTE の CC・SLC_BYPASS、ACE-Lite の設定、MIPS の cache の policy を同じ値に決める。既定は mainline と同じく CC=0、host と共有する FW の object は MIPS で uncached（Procedure referenced: `pvr_mmu.c`・`pvr_vm_mips.c`・`pvr_fw.c`）。「coherent」の結論は、試した属性の組にだけ効く、と書く。
  - SLC については「試験で調べる」をやめる。mainline の手順（完了の通知の後に host が SLC の flush を送らない）を Procedure referenced として採る。CPU が読む物（FWCCB、render の target）は SLC_BYPASS を候補にし、試験は「flush を入れない形で 8 回とも一致する事」を確かめるだけにする。

### Minor

1. **§3.2 の表が壊れている（P2 が読み返していない所）。** design.md:106-112 の表の後に、箇条（113-116）が表の途中に入りました。そのため、元の表の最後の行「| user の blob（Vulkan の memory） | アプリが読み書き | D7 |」（117 行）が、SLC の箇条の続きの文として表示されます。D7 の行が表から消えています。117 行を 112 行の直後に戻し、箇条をその後に置いてください。
2. **D7 (a) の HOST_CACHED の type は、既存の libvulkan では出せない。**
   - libvulkan は HOST_COHERENT の無い type から HOST_VISIBLE と HOST_CACHED を外します（`instance.c:1117-1118`）。確保も ENOTSUP 相当で断ります（`memory.c:1070-1074`）。`vkFlushMappedMemoryRanges`・`vkInvalidateMappedMemoryRanges` は fence だけで、kernel を呼びません（`memory.c:489-535`）。
   - よって design.md:222 の「HOST_CACHED の type は submit の時の FLUSH64 と別に出し」は、非 coherent の type なら黙って device だけの type になります。HOST_COHERENT を名乗らせれば、D7 (b) と同じ仕様違反になります。
   - 直し: D7 (a) は DEVICE_LOCAL と HOST_VISIBLE|HOST_COHERENT（uncached）だけにし、HOST_CACHED は Future Work（libvulkan に renderer の cache の操作の dispatch が要る）に回す。これで design.md:115 の「cacheable の invalidate に頼る所」が無くなります。
   - なお「0b 型の確かめ」という言い方は不正確です。0b は対照であり、invalidate が効く事の確かめではありません。
3. **recovery の細部（design.md:152-157）。**
   - stop_poll が EAGAIN 以外を返した時、core はまず isolate を呼び、失敗した時だけ fault にします（gpu.c:7216-7238）。「device 全体の fault に上げる」は「isolate を経て」と直してください。
   - core が isolate を呼ぶのは「stop を確かめられなかった」時です。FW が context の reset を通知して止めたと分かる context は、stop_poll が 0 を返せる場合です。isolate の成功の条件は「guilty が分かる」ではなく、次の 2 つとしてください。
     - その session の全部の FW の context の到達し得る memory を保持できる。
     - FW が他の context を処理し続けている。
   - 1 つの session（open）は、geometry・fragment・compute・transfer の複数の FW の context と memory context を持ちます。stop_begin・stop_poll は「session の全部の FW の context」を対象にする、と書いてください。
   - health の kick の時間切れ（FW 全体の hang）は、session の stop ではありません。backend が fault に当たる処理をしてから `drv_gpu_report_error` を呼ぶ道（gpu.h の `drv_gpu_report_error` の注）にしてください。
   - 署名は `fault(void *, int)` です。
4. **S9e の値の出典（design.md:32、116）。** FW は mainline の形式なので、KCCB の命令の値は mainline の FWIF（`pvr_rogue_fwif.h:1160` の `ROGUE_FWIF_KCCB_CMD_SLCFLUSHINVAL`）から取るべきです。DDK の `RGXFWIF_*` は、使う時の手順の参照に限ってください（§0.2 と §3.4 の 2 の方針と同じ）。
5. **「WS256 §2.4 をそのまま使う」と、INCONCLUSIVE の規則が違う（design.md:119・124）。** WS256 は INCONCLUSIVE でも主の試験だけで判定します（design-peripherals.md:153）。WS269 は切り替えません。厳しくするのは正しいので、「WS256 との違い」として明記してください。WS256 の 1（CPU→device の dirty の line）と 6（後始末）も同じく参照してください。
6. **古い文の残り。**
   - S10（design.md:29）の「BRN の組が S9c と違う…p002 で照合」は、§0.4 と食い違います。
   - §9（226-227）の「危険: BRN の違い」「未確認: 594 MHz で安定するか」は、§0.4 と F-112 で解決・範囲外になっています。
   - §6 の p002 の「BRN の照合」は、§0.3 の「記録するだけ」と食い違います。
   - S9a の行番号「303-305」は、§3.1 の「302-303」と揃っていません。実物は opp の判定が 302-303、set_rate が 319 です。
7. **予備 (i) の写しのずれと、引き継ぐ制約。**
   - WS256 の ws.md:53 と design-peripherals.md:357 は、今も「p023 が同じ Phase で予備 (i) を実装する」と書いています。phase023/phase.md:12 は直っています（Q1 の範囲の写し）。
   - WS269 の §3.2 の 5 と表は「0x4_0000_0000 + pa の窓」としか書いていません。WS256 §2.4 の予備 (i) の 1〜4 を、p004 が実装する設計として参照してください。
     - 4 KiB の段で、確保した page の別名だけを写す。2 MiB の leaf では OpenSBI の別名まで写る（WS256 review 3 Major 2）。
     - unmap の時に FLUSH64 する。
     - 別名で LR/SC・AMO を使わない。
   - user の NOCACHE で Vulkan のアプリが atomic を使った時の振る舞い（PMA は未確認）も、D7 の注に足してください。
8. **blob の backing の連続性（推測）。** render の target と blob を全部 `drv_dma_alloc_coherent` で取ると、物理で連続の確保になります（`hal_pmem_alloc_limited`）。32 bit の範囲で大きな texture を取ると、断片化で失敗し得ます。GPU には MMU があるので、page ごとの確保と page ごとの別名にするかどうかを p006 の決める事として書いてください。
9. **p005 の依存の表。** WS256 p020 は §6 の文（design.md:203）と ws.md にはありますが、表の p005 の行にはありません。
10. **EBUSY が尽きた時（design.md:133）。** FW は動き、PD も入ったままです。§3.1 の失敗の道（FW を止め、reset・clock・PD を落とす）を同じく通す、と書いてください。

### 確かめられなかった事（推測のまま）

- JH7110 の interconnect が、GPU の CC=1 の transaction を実際に snoop するか。
- FW が job の完了の時に SLC を書き出すか（mainline が SLC の flush を送らない事からの推測）。
- uncached の別名の上での AMO・LR/SC の振る舞い（PMA）。
- uncached の CPU の読みの帯域（今回も未計測）。

### 結論

**今の版のままでは p001 を cleared にしないでください。** 次の局所的な直し（改訂 3.1）を入れれば、design-reviewer の再確認なしで、Q1 が差分を見るだけで cleared にしてよいと判断します。

- 改訂 3.1 で直す物: M1（display の node の読み口と p007 の依存）、M2（試験の requester・属性・SLC の扱い）、Minor 1（壊れた表）、Minor 2（HOST_CACHED を外す）。
- Minor 3〜10 は、改訂 3.1 に入れるか、p004・p006・p007 の phase.md の必須項目にしてください。

p002〜p005（資料の固定、firmware の package、PMU・CRG・別名、FW の起動）の Phase を作って進めることには支障がありません。HAL の API（hal.h）の変更は、M1 で (b) を選ばない限り要らないと判断します。(b) を選ぶ場合は、§3.6 で分析し、要るなら proposed/ に置いて止まってください。
